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
import { type ChildProcess } from "node:child_process";
export declare const UE_PORT: number;
export declare const UE_BASE_URL: string;
export declare const UE_PROJECT_DIR: string;
export declare const REQUEST_TIMEOUT_MS: number;
export declare const state: {
    ueProcess: ChildProcess | null;
    editorMode: boolean;
    startupPromise: Promise<string | null> | null;
};
export declare const log: {
    info: (msg: string, data?: Record<string, unknown>) => void;
    error: (msg: string, data?: Record<string, unknown>) => void;
    debug: (msg: string, data?: Record<string, unknown>) => void;
};
export interface HealthPayload {
    status: string;
    mode: string;
    toolCount?: number;
    pluginVersion?: string;
    engineVersion?: string;
    projectName?: string;
    [key: string]: unknown;
}
/** Returns the health payload if the UE5 server is reachable, or null. */
export declare function getUEHealth(): Promise<HealthPayload | null>;
export declare function isUEHealthy(): Promise<boolean>;
/** Block until the server responds or timeout expires. */
export declare function waitForHealthy(timeoutSeconds?: number): Promise<boolean>;
/** Find the .uproject file in UE_PROJECT_DIR. */
export declare function findUProject(): string | null;
/** Read EngineAssociation from the .uproject file. */
export declare function readEngineVersion(): string | null;
/** Locate UnrealEditor-Cmd.exe on disk. */
export declare function findEditorCmd(): string | null;
/** Spawn UE5 commandlet and wait for it to become healthy. */
export declare function spawnAndWait(): Promise<string | null>;
/**
 * Ensure the UE5 server is running.
 *
 * If the editor is already running with the plugin, we connect to it.
 * Otherwise, spawn a commandlet in the background.
 */
export declare function ensureUE(): Promise<string | null>;
/** GET request to the UE5 server. */
export declare function ueGet(endpoint: string, params?: Record<string, string>): Promise<unknown>;
/** POST request to the UE5 server. */
export declare function uePost(endpoint: string, body?: Record<string, unknown>): Promise<unknown>;
/** Ask the UE5 server to shut down gracefully. */
export declare function gracefulShutdown(): Promise<void>;
