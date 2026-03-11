/**
 * Tool Router — dynamic tool classification and mega-tool collapsing
 *
 * Adapted from UnrealClaude's tool-router.js.
 *
 * The C++ server exposes 150+ tools. Listing them all would burn the LLM's
 * context window. The router classifies tools into three tiers:
 *
 *   - Simple: listed individually with full schemas (~15 most-used tools)
 *   - Hidden: callable by name but never listed (task queue, scripting)
 *   - Mega:   collapsed into category router tools (blueprint, material, etc.)
 *
 * Tool classification is fetched dynamically from /api/tools — the categories
 * below are pattern-matched from tool names, so new C++ tools automatically
 * slot into the right bucket.
 */
export interface UEToolParam {
    name: string;
    type: string;
    description?: string;
    required?: boolean;
    default?: unknown;
}
export interface UETool {
    name: string;
    description: string;
    category?: string;
    parameters?: UEToolParam[];
    annotations?: {
        readOnlyHint?: boolean;
        destructiveHint?: boolean;
        idempotentHint?: boolean;
        openWorldHint?: boolean;
    };
}
export type ToolTier = "simple" | "hidden" | "mega";
export interface ToolCategory {
    label: string;
    description: string;
    toolPatterns: RegExp[];
}
export declare const CATEGORIES: Record<string, ToolCategory>;
/** Classify a tool into simple / hidden / mega. */
export declare function classifyTool(toolName: string): ToolTier;
/** Find the category key for a mega-tool. Returns null if uncategorized. */
export declare function getCategoryForTool(toolName: string): string | null;
/** Group all tools by category for status display. */
export declare function categorizeTool(toolName: string): string;
/** Convert UE tool parameters to a JSON Schema object. */
export declare function convertToMCPSchema(params: UEToolParam[] | undefined, compact?: boolean): Record<string, unknown>;
/** Convert UE annotations to MCP tool annotations. */
export declare function convertAnnotations(annotations?: UETool["annotations"]): Record<string, boolean>;
/**
 * Build a mega-tool schema for a category, listing all the collapsed tools
 * underneath it so the LLM knows what operations are available.
 */
export declare function buildCategoryRouterSchema(categoryKey: string, tools: UETool[]): Record<string, unknown> | null;
