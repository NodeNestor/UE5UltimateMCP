/**
 * Formatting helpers for tool outputs
 *
 * Converts raw UE5 server responses into well-structured MCP content blocks.
 */

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Core formatter
// ---------------------------------------------------------------------------

/**
 * Format a UE5 tool result into MCP response content blocks.
 *
 * - Detects image_base64 in capture_viewport responses and returns native ImageContent
 * - Attaches optional context docs for relevant tools
 */
export function formatToolResponse(
  toolName: string,
  result: UEToolResult,
  contextProvider?: (toolName: string) => string | null,
): MCPResponse {
  if (!result.success) {
    const errorMsg = result.error || result.message || "Unknown error";
    return {
      content: [{ type: "text", text: `Error: ${errorMsg}` }],
      isError: true,
    };
  }

  const content: MCPContent[] = [];

  // Special handling for viewport captures — return inline image
  if (
    toolName === "capture_viewport" &&
    result.data &&
    typeof result.data === "object" &&
    "image_base64" in (result.data as Record<string, unknown>)
  ) {
    const imgData = result.data as Record<string, unknown>;
    const imgContent: ImageContent = {
      type: "image",
      data: imgData.image_base64 as string,
      mimeType: `image/${(imgData.format as string) || "jpeg"}`,
    };
    content.push(imgContent);
    // Include metadata without the huge base64 blob
    const meta = { ...imgData };
    delete meta.image_base64;
    if (result.message || Object.keys(meta).length > 0) {
      const metaText: TextContent = {
        type: "text",
        text: [result.message, Object.keys(meta).length > 0 ? JSON.stringify(meta) : ""]
          .filter(Boolean)
          .join("\n\n"),
      };
      content.push(metaText);
    }
  } else {
    let text = result.message || "";
    if (result.data !== undefined && result.data !== null) {
      const dataStr =
        typeof result.data === "string" ? result.data : JSON.stringify(result.data, null, 2);
      text = text ? `${text}\n\n${dataStr}` : dataStr;
    }

    // Inject relevant UE5 context docs if available
    if (contextProvider) {
      const ctx = contextProvider(toolName);
      if (ctx) {
        text += `\n\n---\n\n## Relevant UE5 API Context\n\n${ctx}`;
      }
    }

    if (text) {
      content.push({ type: "text", text });
    }
  }

  // Fallback if we somehow have no content
  if (content.length === 0) {
    content.push({ type: "text", text: "OK" });
  }

  return { content, isError: false };
}

// ---------------------------------------------------------------------------
// Utility formatters
// ---------------------------------------------------------------------------

/** Format a list of items as a numbered list. */
export function formatList(items: string[], header?: string): string {
  const lines: string[] = [];
  if (header) lines.push(`## ${header}\n`);
  items.forEach((item, i) => lines.push(`${i + 1}. ${item}`));
  return lines.join("\n");
}

/** Format a key-value record as a Markdown table. */
export function formatTable(data: Record<string, unknown>, header?: string): string {
  const lines: string[] = [];
  if (header) lines.push(`## ${header}\n`);
  lines.push("| Key | Value |");
  lines.push("|-----|-------|");
  for (const [key, value] of Object.entries(data)) {
    const valStr = typeof value === "object" ? JSON.stringify(value) : String(value ?? "");
    lines.push(`| ${key} | ${valStr} |`);
  }
  return lines.join("\n");
}

/** Truncate a string to maxLen, appending "..." if truncated. */
export function truncate(str: string, maxLen: number): string {
  if (str.length <= maxLen) return str;
  return str.slice(0, maxLen - 3) + "...";
}

/** Pretty-print JSON with a size guard. */
export function safeStringify(data: unknown, maxLen = 50000): string {
  const json = JSON.stringify(data, null, 2);
  if (json.length > maxLen) {
    return json.slice(0, maxLen) + "\n\n... (truncated, total " + json.length + " chars)";
  }
  return json;
}
