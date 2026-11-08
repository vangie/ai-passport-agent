import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer', ROOT / 'tools/openclaw/install-passport-relay.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def test_patch_is_scoped_and_preserves_original(self):
        source = '''function broadcastToOwner$1(context, connId, event) {
}
function closeRelaySession(session, reason, options) {
}
function sendAudio() {
\tconst audio = decodeTalkRelayAudioBase64(params.audioBase64, "Realtime relay");
}
function forceConsult(session, text) {
    return {
        responseStyle: "Reply in a concise spoken tone."
    };
}
function speakConsult(session, text) {
    session.bridge.sendUserMessage(FORCED_CONSULT_CHECKING_PROMPT);
    session.bridge.sendUserMessage(buildForcedConsultSpeechPrompt(text));
}
reply({
\t\t\t\t\tvoiceSessionId: session.relaySessionId,
\t\t\t\t\tmode,
});
function createTalkRealtimeRelaySession(params) {
\tconst emit = (event, talkEvent) => broadcastToOwner$1(params.context, params.connId, {
...event
});
const relay = {
\t\tgetToolAuthorityOverlay: consultRunner.getToolAuthorityOverlay,
};
const bridgeRequest = {
\t\tinstructions: params.instructions,
};
broadcastToOwner$1(params.context, params.connId, {type: "audioDone"});
}
//#endregion
\t\t\t\tconst realtimeConfig = buildTalkRealtimeConfig(runtimeConfig, requested.provider, requested.model);
resolve({autoRespondToAudio: realtimeConfig.consultRouting !== "force-agent-consult"
\t\t\t\t});
const forceAgentConsultOnFinalTranscript = realtimeConfig.consultRouting === "force-agent-consult";
create({
\t\t\t\t\tforceAgentConsultOnFinalTranscript
});
'''
        patched = installer.patch(source)
        self.assertIn('deviceFamily === "ai-passport"', patched)
        self.assertIn('enabled: params.passportClient === true', patched)
        self.assertIn('const passportRouting = resolvePassportConsultRouting(realtimeConfig.consultRouting, passportClient);', patched)
        self.assertIn('autoRespondToAudio: passportRouting.autoRespondToAudio', patched)
        self.assertIn('const forceAgentConsultOnFinalTranscript = passportRouting.forceAgentConsultOnFinalTranscript;', patched)
        self.assertNotIn('autoRespondToAudio: !passportClient', patched)
        self.assertIn('session.passportOutput?.close();', patched)
        self.assertIn('event = output.project(event);', patched)
        self.assertIn('passportSilence: true, passportIma', patched)
        self.assertIn('caps?.includes("passport-ima-v1")', patched)
        self.assertIn('ima: params.passportIma === true', patched)
        self.assertIn('session.passportOutput?.decodeAudio(params.audioBase64)', patched)
        self.assertIn('instructions: withPassportVoiceInstructions(params.instructions, params.language, params.passportClient === true)', patched)
        self.assertIn('passportLanguage: params.language', patched)
        self.assertIn('passportClient: params.passportClient === true', patched)
        self.assertIn('withPassportVoiceLanguage(FORCED_CONSULT_CHECKING_PROMPT, session.passportLanguage, session.passportClient === true)', patched)
        self.assertIn('withPassportVoiceLanguage(buildForcedConsultSpeechPrompt(text), session.passportLanguage, session.passportClient === true)', patched)
        self.assertIn('responseStyle: withPassportVoiceLanguage("Reply in a concise spoken tone.", session.passportLanguage, session.passportClient === true)', patched)
        self.assertIn('passportOutput.send({type: "audioDone"})', patched)
        self.assertIn('send: (event) => broadcastToOwner$1(params.context, params.connId, event)', patched)
        self.assertNotIn('passportOutput.send(event)', patched)
        with self.assertRaises(ValueError):
            installer.patch(patched)
        with self.assertRaises(ValueError):
            installer.patch(source.replace('const realtimeConfig =', 'const changedConfig ='))

    def test_atomic_write_and_digest(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'file'
            installer.atomic_write(path, b'old')
            installer.atomic_write(path, b'new')
            self.assertEqual(path.read_bytes(), b'new')
            self.assertEqual(installer.digest(b'new'), hashlib.sha256(b'new').hexdigest())
            self.assertEqual(len(list(Path(temporary).iterdir())), 1)


if __name__ == '__main__':
    unittest.main()
