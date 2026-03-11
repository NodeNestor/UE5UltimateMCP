/**
 * Formatting helpers for tool outputs
 *
 * Converts raw UE5 server responses into well-structured MCP content blocks.
 */
export interface UEToolResult {
    success: boolean;
    message?: string;
    data?: unknown;
    error?: string;
}
export interface TextContent {
    type: "text";
    text: string;
}
export interface ImageContent {
    type: "image";
    data: string;
    mimeType: string;
}
export type MCPContent = TextContent | ImageContent;
export interface MCPResponse {
    content: MCPContent[];
    isError?: boolean;
}
/**
 * Format a UE5 tool result into MCP response content blocks.
 *
 * - Detects image_base64 in capture_viewport responses and returns native ImageContent
 * - Attaches optional context docs for relevant tools
 */
export declare function formatToolResponse(toolName: string, result: UEToolResult, contextProvider?: (toolName: string) => string | null): MCPResponse;
/** Format a list of items as a numbered list. */
export declare function formatList(items: string[], header?: string): string;
/** Format a key-value record as a Markdown table. */
export declare function formatTable(data: Record<string, unknown>, header?: string): string;
/** Truncate a string to maxLen, appending "..." if truncated. */
export declare function truncate(str: string, maxLen: number): string;
/** Pretty-print JSON with a size guard. */
export declare function safeStringify(data: unknown, maxLen?: number): string;
