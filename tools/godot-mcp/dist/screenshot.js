import { execFile } from "node:child_process";
import { join } from "node:path";
import { existsSync, statSync } from "node:fs";
import { PKG_ROOT } from "./godot.js";
export function captureWindow(pid, outPath, timeoutMs = 30000) {
    const script = join(PKG_ROOT, "scripts", "screenshot.ps1");
    return new Promise((resolve, reject) => {
        execFile("powershell", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script, "-ProcId", String(pid), "-OutPath", outPath], { timeout: timeoutMs }, (err, stdout, stderr) => {
            if (err)
                return reject(new Error(`Screenshot failed: ${err.message}\n${stderr}\n${stdout}`));
            if (!existsSync(outPath))
                return reject(new Error(`Screenshot file was not created.\n${stdout}\n${stderr}`));
            const bytes = statSync(outPath).size;
            if (bytes < 500)
                return reject(new Error(`Screenshot file is suspiciously small (${bytes} bytes)`));
            resolve({ path: outPath, bytes });
        });
    });
}
