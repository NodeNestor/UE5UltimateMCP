/**
 * Formatting helpers for tool outputs
 *
 * Converts raw UE5 server responses into well-structured MCP content blocks.
 */
// ---------------------------------------------------------------------------
// Core formatter
// ---------------------------------------------------------------------------
/**
 * Format a UE5 tool result into MCP response content blocks.
 *
 * - Detects image_base64 in capture_viewport responses and returns native ImageContent
 * - Attaches optional context docs for relevant tools
 */
export function formatToolResponse(toolName, result, contextProvider) {
    if (!result.success) {
        const errorMsg = result.error || result.message || "Unknown error";
        return {
            content: [{ type: "text", text: `Error: ${errorMsg}` }],
            isError: true,
        };
    }
    const content = [];
    // Special handling for viewport captures — return inline image
    if (toolName === "capture_viewport" &&
        result.data &&
        typeof result.data === "object" &&
        "image_base64" in result.data) {
        const imgData = result.data;
        const imgContent = {
            type: "image",
            data: imgData.image_base64,
            mimeType: `image/${imgData.format || "jpeg"}`,
        };
        content.push(imgContent);
        // Include metadata without the huge base64 blob
        const meta = { ...imgData };
        delete meta.image_base64;
        if (result.message || Object.keys(meta).length > 0) {
            const metaText = {
                type: "text",
                text: [result.message, Object.keys(meta).length > 0 ? JSON.stringify(meta) : ""]
                    .filter(Boolean)
                    .join("\n\n"),
            };
            content.push(metaText);
        }
    }
    else {
        let text = result.message || "";
        if (result.data !== undefined && result.data !== null) {
            const dataStr = typeof result.data === "string" ? result.data : JSON.stringify(result.data, null, 2);
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
export function formatList(items, header) {
    const lines = [];
    if (header)
        lines.push(`## ${header}\n`);
    items.forEach((item, i) => lines.push(`${i + 1}. ${item}`));
    return lines.join("\n");
}
/** Format a key-value record as a Markdown table. */
export function formatTable(data, header) {
    const lines = [];
    if (header)
        lines.push(`## ${header}\n`);
    lines.push("| Key | Value |");
    lines.push("|-----|-------|");
    for (const [key, value] of Object.entries(data)) {
        const valStr = typeof value === "object" ? JSON.stringify(value) : String(value ?? "");
        lines.push(`| ${key} | ${valStr} |`);
    }
    return lines.join("\n");
}
/** Truncate a string to maxLen, appending "..." if truncated. */
export function truncate(str, maxLen) {
    if (str.length <= maxLen)
        return str;
    return str.slice(0, maxLen - 3) + "...";
}
/** Pretty-print JSON with a size guard. */
export function safeStringify(data, maxLen = 50000) {
    const json = JSON.stringify(data, null, 2);
    if (json.length > maxLen) {
        return json.slice(0, maxLen) + "\n\n... (truncated, total " + json.length + " chars)";
    }
    return json;
}
//# sourceMappingURL=helpers.js.map