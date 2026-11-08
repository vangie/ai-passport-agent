#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f"Unsupported gateway source boundary: {old[:72]!r}")
    return source.replace(old, new, 1)


def patch(source):
    source = replace_once(source, '\tconst audio = decodeTalkRelayAudioBase64(params.audioBase64, "Realtime relay");',
                          '\tconst audio = session.passportOutput?.decodeAudio(params.audioBase64) ?? decodeTalkRelayAudioBase64(params.audioBase64, "Realtime relay");')
    source = replace_once(source, '\t\t\t\t\tvoiceSessionId: session.relaySessionId,\n\t\t\t\t\tmode,',
                          '\t\t\t\t\tvoiceSessionId: session.relaySessionId,\n\t\t\t\t\t...passportClient ? { passportSilence: true, passportIma } : {},\n\t\t\t\t\tmode,')
    source = replace_once(source, 'function broadcastToOwner$1(context, connId, event) {',
                          'function broadcastToOwner$1(context, connId, event) {\n\tconst output = relaySessions.get(event.relaySessionId)?.passportOutput;\n\tif (output) {\n\t\tevent = output.project(event);\n\t\tif (!event) return;\n\t}')
    source = replace_once(source, 'function closeRelaySession(session, reason, options) {',
                          'function closeRelaySession(session, reason, options) {\n\tsession.passportOutput?.close();')
    begin = source.index('function createTalkRealtimeRelaySession(params) {')
    end = source.index('//#endregion', begin)
    section = source[begin:end]
    section = replace_once(section, '\tconst emit = (event, talkEvent) => broadcastToOwner$1(params.context, params.connId, {',
                          '\tconst passportOutput = createPassportRelayOutput({\n\t\tenabled: params.passportClient === true,\n\t\tima: params.passportIma === true,\n\t\tsend: (event) => broadcastToOwner$1(params.context, params.connId, event),\n\t\tfail: (error) => relayRef.current?.failSession(error.message)\n\t});\n\tconst emit = (event, talkEvent) => passportOutput.send({')
    section = section.replace('broadcastToOwner$1(params.context, params.connId, {', 'passportOutput.send({')
    section = replace_once(section, '\t\tinstructions: params.instructions,',
                           '\t\tinstructions: withPassportVoiceInstructions(params.instructions, params.language, params.passportClient === true),')
    section = replace_once(section, '\t\tgetToolAuthorityOverlay: consultRunner.getToolAuthorityOverlay,',
                           '\t\tpassportOutput,\n\t\tpassportClient: params.passportClient === true,\n\t\tpassportLanguage: params.language,\n\t\tgetToolAuthorityOverlay: consultRunner.getToolAuthorityOverlay,')
    source = source[:begin] + section + source[end:]
    source = replace_once(source, 'responseStyle: "Reply in a concise spoken tone."',
                          'responseStyle: withPassportVoiceLanguage("Reply in a concise spoken tone.", session.passportLanguage, session.passportClient === true)')
    source = replace_once(source, 'session.bridge.sendUserMessage(FORCED_CONSULT_CHECKING_PROMPT)',
                          'session.bridge.sendUserMessage(withPassportVoiceLanguage(FORCED_CONSULT_CHECKING_PROMPT, session.passportLanguage, session.passportClient === true))')
    source = replace_once(source, 'session.bridge.sendUserMessage(buildForcedConsultSpeechPrompt(text))',
                          'session.bridge.sendUserMessage(withPassportVoiceLanguage(buildForcedConsultSpeechPrompt(text), session.passportLanguage, session.passportClient === true))')
    source = replace_once(source, '\t\t\t\tconst realtimeConfig = buildTalkRealtimeConfig(runtimeConfig, requested.provider, requested.model);',
                          '\t\t\t\tconst realtimeConfig = buildTalkRealtimeConfig(runtimeConfig, requested.provider, requested.model);\n\t\t\t\tconst passportClient = client?.connect?.client?.deviceFamily === "ai-passport";\n\t\t\t\tconst passportIma = passportClient && client?.connect?.caps?.includes("passport-ima-v1") === true;\n\t\t\t\tconst passportRouting = resolvePassportConsultRouting(realtimeConfig.consultRouting, passportClient);')
    source = replace_once(source, 'autoRespondToAudio: realtimeConfig.consultRouting !== "force-agent-consult"\n\t\t\t\t});',
                          'autoRespondToAudio: passportRouting.autoRespondToAudio\n\t\t\t\t});')
    source = replace_once(source, 'const forceAgentConsultOnFinalTranscript = realtimeConfig.consultRouting === "force-agent-consult";',
                          'const forceAgentConsultOnFinalTranscript = passportRouting.forceAgentConsultOnFinalTranscript;')
    source = replace_once(source, '\t\t\t\t\tforceAgentConsultOnFinalTranscript\n',
                          '\t\t\t\t\tpassportClient,\n\t\t\t\t\tpassportIma,\n\t\t\t\t\tforceAgentConsultOnFinalTranscript\n')
    return 'import {createPassportRelayOutput, resolvePassportConsultRouting, withPassportVoiceInstructions, withPassportVoiceLanguage} from "./passport-relay.mjs";\n' + source


def digest(data):
    return hashlib.sha256(data).hexdigest()


def atomic_write(path, data):
    temporary = path.with_name(path.name + '.passport-tmp')
    temporary.write_bytes(data)
    os.replace(temporary, path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('dist', type=Path)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--restore', action='store_true')
    parser.add_argument('--expected-sha')
    args = parser.parse_args()
    candidates = [path for path in args.dist.glob('handlers-*.mjs')
                  if 'function createTalkRealtimeRelaySession(params)' in path.read_text()]
    if len(candidates) != 1:
        raise ValueError('Expected exactly one installed realtime handler')
    target = candidates[0]
    original = target.read_bytes()
    original_sha = digest(original)
    module = Path(__file__).with_name('passport-relay.mjs').read_bytes()
    codec = Path(__file__).with_name('passport-ima.mjs').read_bytes()
    receipt_path = args.dist / 'passport-relay-patch.json'
    if receipt_path.exists():
        receipt = json.loads(receipt_path.read_text())
        installed_module = args.dist / 'passport-relay.mjs'
        installed_codec = args.dist / 'passport-ima.mjs'
        codec_matches = 'codec_sha256' not in receipt or installed_codec.is_file() and digest(installed_codec.read_bytes()) == receipt['codec_sha256']
        if original_sha == receipt['patched_sha256'] and installed_module.is_file() and digest(installed_module.read_bytes()) == receipt['module_sha256'] and codec_matches:
            if args.restore:
                backup = args.dist / '.passport-relay-backups' / receipt['original_sha256'] / target.name
                data = backup.read_bytes()
                if digest(data) != receipt['original_sha256']:
                    raise ValueError('Original handler backup checksum mismatch')
                atomic_write(target, data)
                receipt_path.unlink()
                (args.dist / 'passport-relay.mjs').unlink()
                if 'codec_sha256' in receipt:
                    installed_codec.unlink()
                print(json.dumps({'status': 'restored-restart-required', 'handler': target.name}))
                return
            if digest(module) == receipt['module_sha256'] and digest(codec) == receipt.get('codec_sha256'):
                print(json.dumps({'status': 'already-applied', 'handler': target.name}))
                return
        raise ValueError('Existing patch or gateway was changed; reconcile backup before installing')
    if args.restore:
        raise ValueError('No installed patch receipt to restore')
    patched = patch(original.decode()).encode()
    with tempfile.NamedTemporaryFile(suffix='.mjs') as check:
        check.write(patched)
        check.flush()
        subprocess.run(['node', '--check', check.name], check=True)
    report = {'handler': target.name, 'original_sha256': original_sha,
              'patched_sha256': digest(patched), 'module_sha256': digest(module), 'codec_sha256': digest(codec),
              'scope': 'ai-passport authenticated client family only'}
    if args.apply:
        if args.expected_sha != original_sha:
            raise ValueError('Apply requires the exact --expected-sha reported by the dry run')
        destination = args.dist / 'passport-relay.mjs'
        codec_destination = args.dist / 'passport-ima.mjs'
        if destination.exists() or codec_destination.exists():
            raise ValueError('Refusing to overwrite an unrelated relay module')
        backup = args.dist / '.passport-relay-backups' / original_sha
        backup.mkdir(parents=True, exist_ok=True)
        backup_file = backup / target.name
        if backup_file.exists() and digest(backup_file.read_bytes()) != original_sha:
            raise ValueError('Existing original handler backup checksum mismatch')
        if not backup_file.exists():
            shutil.copy2(target, backup_file)
        atomic_write(destination, module)
        atomic_write(codec_destination, codec)
        atomic_write(target, patched)
        atomic_write(receipt_path, json.dumps(report, indent=2).encode() + b'\n')
        report['status'] = 'applied-restart-required'
    else:
        report['status'] = 'dry-run'
    print(json.dumps(report))


if __name__ == '__main__':
    main()
