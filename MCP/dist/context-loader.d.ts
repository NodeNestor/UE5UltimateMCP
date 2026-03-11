/**
 * Dynamic Context Loader
 *
 * Adapted from UnrealClaude's context-loader.js.
 *
 * Loads UE5 API context documentation from MCP/contexts/*.md files
 * and returns relevant sections based on tool names or keyword queries.
 * This gives the LLM reference material for UE5 APIs without bloating
 * every tool call's prompt.
 */
/** Get context category from a tool name. */
export declare function getCategoryFromTool(toolName: string): string | null;
/** Find all matching categories for a freeform query. */
export declare function getCategoriesFromQuery(query: string): string[];
/** Load combined context content for a category. */
export declare function loadContextForCategory(category: string): string | null;
/** Get context for a tool call (automatic injection). */
export declare function getContextForTool(toolName: string): string | null;
/** Get context for a freeform query. */
export declare function getContextForQuery(query: string): {
    categories: string[];
    content: string;
} | null;
/** List all available context categories. */
export declare function listCategories(): string[];
/** Get metadata about a category. */
export declare function getCategoryInfo(category: string): {
    name: string;
    files: string[];
    keywords: string[];
    hasContent: boolean;
} | null;
/** List all context files that actually exist on disk. */
export declare function listAvailableContextFiles(): string[];
/** Clear the file cache (useful for hot-reloading). */
export declare function clearCache(): void;
