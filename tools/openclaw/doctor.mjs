// Compatibility command. The shared checkout owns the implementation.
import {existsSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const source = new URL('../../integrations/valet-gateway/tools/openclaw/doctor.mjs', import.meta.url);
if (!existsSync(source)) {
    console.error('Shared gateway source is missing. Run: git submodule update --init --recursive');
    process.exit(1);
}
const result = spawnSync(process.execPath, [fileURLToPath(source), ...process.argv.slice(2)], {stdio: 'inherit'});
if (result.error) console.error(result.error.message);
process.exit(result.status ?? 1);
