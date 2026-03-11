/**
 * UE5 HTTP Bridge
 *
 * Handles all communication between the MCP wrapper and the C++ HTTP server
 * running inside Unreal Engine on port 9847 (configurable via UE_PORT).
 *
 * Adapted from BlueprintMCP's ue-bridge.ts — simplified for UltimateMCP
 * since the C++ server handles all engine logic. This bridge is a thin
 * HTTP relay with health checks and auto-detection.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { spawn } from "node:child_process";
// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
export const UE_PORT = parseInt(process.env.UE_PORT || "9847", 10);
export const UE_BASE_URL = `http://localhost:${UE_PORT}`;
export const UE_PROJECT_DIR = process.env.UE_PROJECT_DIR || process.cwd();
export const REQUEST_TIMEOUT_MS = parseInt(process.env.UE_TIMEOUT_MS || "300000", 10);
// ---------------------------------------------------------------------------
// Mutable state
// ---------------------------------------------------------------------------
export const state = {
    ueProcess: null,
    editorMode: false,
    startupPromise: null,
};
// ---------------------------------------------------------------------------
// Logging (stderr only — stdout is reserved for MCP protocol)
// ---------------------------------------------------------------------------
export const log = {
    info: (msg, data) => console.error(`[UE5MCP] ${msg}`, data ? JSON.stringify(data) : ""),
    error: (msg, data) => console.error(`[UE5MCP:ERROR] ${msg}`, data ? JSON.stringify(data) : ""),
    debug: (msg, data) => {
        if (process.env.DEBUG) {
            console.error(`[UE5MCP:DEBUG] ${msg}`, data ? JSON.stringify(data) : "");
        }
    },
};
/** Returns the health payload if the UE5 server is reachable, or null. */
export async function getUEHealth() {
    try {
        const resp = await fetch(`${UE_BASE_URL}/api/health`, {
            signal: AbortSignal.timeout(3000),
        });
        if (!resp.ok)
            return null;
        return (await resp.json());
    }
    catch {
        return null;
    }
}
export async function isUEHealthy() {
    return (await getUEHealth()) !== null;
}
/** Block until the server responds or timeout expires. */
export async function waitForHealthy(timeoutSeconds = 180) {
    const deadline = Date.now() + timeoutSeconds * 1000;
    while (Date.now() < deadline) {
        if (await isUEHealthy())
            return true;
        if (!state.ueProcess)
            return false;
        await new Promise((r) => setTimeout(r, 2000));
    }
    return false;
}
// ---------------------------------------------------------------------------
// UE5 Process Management
// ---------------------------------------------------------------------------
/** Find the .uproject file in UE_PROJECT_DIR. */
export function findUProject() {
    try {
        const entries = fs.readdirSync(UE_PROJECT_DIR);
        const uprojectFile = entries.find((e) => e.endsWith(".uproject"));
        if (uprojectFile)
            return path.join(UE_PROJECT_DIR, uprojectFile);
    }
    catch {
        /* ignore */
    }
    return null;
}
/** Read EngineAssociation from the .uproject file. */
export function readEngineVersion() {
    const uproject = findUProject();
    if (!uproject)
        return null;
    try {
        const data = JSON.parse(fs.readFileSync(uproject, "utf-8"));
        if (typeof data.EngineAssociation === "string" && data.EngineAssociation) {
            return data.EngineAssociation;
        }
    }
    catch {
        /* ignore */
    }
    return null;
}
/** Locate UnrealEditor-Cmd.exe on disk. */
export function findEditorCmd() {
    if (process.env.UE_EDITOR_CMD && fs.existsSync(process.env.UE_EDITOR_CMD)) {
        return process.env.UE_EDITOR_CMD;
    }
    const engineVersion = readEngineVersion();
    if (engineVersion) {
        const candidates = [
            `C:\\Program Files\\Epic Games\\UE_${engineVersion}\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe`,
            `C:\\Program Files (x86)\\Epic Games\\UE_${engineVersion}\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe`,
        ];
        for (const p of candidates) {
            if (fs.existsSync(p)) {
                log.info(`Auto-detected engine ${engineVersion} from .uproject`);
                return p;
            }
        }
    }
    // Fallback: scan Epic Games directory
    const epicDir = "C:\\Program Files\\Epic Games";
    try {
        const entries = fs.readdirSync(epicDir);
        for (const entry of entries.sort().reverse()) {
            if (entry.startsWith("UE_")) {
                const candidate = path.join(epicDir, entry, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe");
                if (fs.existsSync(candidate)) {
                    log.info(`Found engine ${entry.replace("UE_", "")}`);
                    return candidate;
                }
            }
        }
    }
    catch {
        /* directory may not exist */
    }
    return null;
}
/** Spawn UE5 commandlet and wait for it to become healthy. */
export async function spawnAndWait() {
    const editorCmd = findEditorCmd();
    if (!editorCmd) {
        return "Could not find UnrealEditor-Cmd.exe. Set UE_EDITOR_CMD environment variable.";
    }
    const uproject = findUProject();
    if (!uproject) {
        return `No .uproject file found in ${UE_PROJECT_DIR}`;
    }
    const logPath = path.join(UE_PROJECT_DIR, "Saved", "Logs", "UE5UltimateMCP_server.log");
    log.info("Spawning UE5 commandlet...");
    state.ueProcess = spawn(editorCmd, [uproject, "-run=UltimateMCP", `-port=${UE_PORT}`, "-unattended", "-nopause", "-nullrhi", `-LOG=${logPath}`], { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
    state.ueProcess.on("exit", (code) => {
        log.info(`UE5 server exited with code ${code}`);
        state.ueProcess = null;
    });
    state.ueProcess.stdout?.on("data", (data) => {
        log.debug(`[UE5:out] ${data.toString().trim()}`);
    });
    state.ueProcess.stderr?.on("data", (data) => {
        log.debug(`[UE5:err] ${data.toString().trim()}`);
    });
    log.info("Waiting for health check (up to 3 min)...");
    const ok = await waitForHealthy(180);
    if (ok) {
        log.info("UE5 server is ready.");
        return null;
    }
    if (state.ueProcess) {
        state.ueProcess.kill();
        state.ueProcess = null;
    }
    return "UE5 server failed to start within 3 minutes. Check Saved/Logs/UE5UltimateMCP_server.log.";
}
/**
 * Ensure the UE5 server is running.
 *
 * If the editor is already running with the plugin, we connect to it.
 * Otherwise, spawn a commandlet in the background.
 */
export async function ensureUE() {
    const health = await getUEHealth();
    if (health) {
        state.editorMode = health.mode === "editor";
        log.info("Connected to existing UE5 server", {
            mode: health.mode,
            toolCount: health.toolCount,
        });
        return null;
    }
    state.editorMode = false;
    // Deduplicate concurrent startup calls
    if (state.startupPromise) {
        return state.startupPromise;
    }
    if (state.ueProcess) {
        log.info("UE5 process exists but unhealthy — killing and respawning...");
        state.ueProcess.kill();
        state.ueProcess = null;
    }
    state.startupPromise = spawnAndWait().finally(() => {
        state.startupPromise = null;
    });
    return state.startupPromise;
}
// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------
/** GET request to the UE5 server. */
export async function ueGet(endpoint, params = {}) {
    const url = new URL(endpoint, UE_BASE_URL);
    for (const [k, v] of Object.entries(params)) {
        if (v !== undefined && v !== null)
            url.searchParams.set(k, v);
    }
    const resp = await fetch(url.toString(), {
        signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });
    if (!resp.ok) {
        throw new Error(`UE5 GET ${endpoint} failed: HTTP ${resp.status} ${resp.statusText}`);
    }
    return resp.json();
}
/** POST request to the UE5 server. */
export async function uePost(endpoint, body = {}) {
    const resp = await fetch(`${UE_BASE_URL}${endpoint}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
        signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });
    if (!resp.ok) {
        throw new Error(`UE5 POST ${endpoint} failed: HTTP ${resp.status} ${resp.statusText}`);
    }
    return resp.json();
}
/** Ask the UE5 server to shut down gracefully. */
export async function gracefulShutdown() {
    try {
        await fetch(`${UE_BASE_URL}/api/shutdown`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: "{}",
            signal: AbortSignal.timeout(3000),
        });
    }
    catch {
        /* server may already be gone */
    }
    if (state.ueProcess) {
        const proc = state.ueProcess;
        const exited = await new Promise((resolve) => {
            const timer = setTimeout(() => resolve(false), 15000);
            proc.on("exit", () => {
                clearTimeout(timer);
                resolve(true);
            });
        });
        if (!exited && state.ueProcess) {
            log.info("Graceful shutdown timed out, force-killing.");
            state.ueProcess.kill();
        }
        state.ueProcess = null;
    }
}
//# sourceMappingURL=ue-bridge.js.map