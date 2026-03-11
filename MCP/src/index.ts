#!/usr/bin/env node

/**
 * UE5 Ultimate MCP Server
 *
 * Main entry point. Bridges Claude Code (MCP over stdio) to the C++ HTTP
 * server running inside Unreal Engine.
 *
 * Key design: the tool list is fetched dynamically from /api/tools at startup.
 * Adding new C++ tools automatically makes them available — no TS changes needed.
 *
 * Environment Variables:
 *   UE_PORT            - UE5 server port (default: 9847)
 *   UE_PROJECT_DIR     - Path to .uproject directory
 *   UE_TIMEOUT_MS      - HTTP request timeout (default: 300000)
 *   INJECT_CONTEXT     - Auto-inject context docs on tool calls (default: false)
 *   DEBUG              - Enable debug logging
 */

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

import {
  UE_PORT,
  UE_BASE_URL,
  getUEHealth,
  ensureUE,
  uePost,
  gracefulShutdown,
  log,
} from "./ue-bridge.js";

import {
  type UETool,
  classifyTool,
  getCategoryForTool,
  categorizeTool,
  convertToMCPSchema,
  convertAnnotations,
  buildCategoryRouterSchema,
  CATEGORIES,
} from "./tool-router.js";

import {
  getContextForTool,
  getContextForQuery,
  loadContextForCategory,
  listCategories,
  getCategoryInfo,
  listAvailableContextFiles,
} from "./context-loader.js";

import { formatToolResponse, safeStringify, type MCPResponse } from "./helpers.js";

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

const INJECT_CONTEXT = process.env.INJECT_CONTEXT === "true";
const TOOL_CACHE_TTL_MS = parseInt(process.env.MCP_TOOL_CACHE_TTL_MS || "30000", 10);

// ---------------------------------------------------------------------------
// Tool cache
// ---------------------------------------------------------------------------

interface ToolCache {
  tools: UETool[];
  timestamp: number;
}

let toolCache: ToolCache = { tools: [], timestamp: 0 };

async function fetchToolList(): Promise<UETool[]> {
  const cacheAge = Date.now() - toolCache.timestamp;
  if (toolCache.tools.length > 0 && cacheAge < TOOL_CACHE_TTL_MS) {
    log.debug("Using cached tool list", { ageMs: cacheAge, count: toolCache.tools.length });
    return toolCache.tools;
  }

  try {
    const resp = await fetch(`${UE_BASE_URL}/api/tools`, {
      signal: AbortSignal.timeout(10000),
    });
    if (!resp.ok) {
      log.error("Failed to fetch tool list", { status: resp.status });
      return toolCache.tools; // return stale cache
    }
    const data = (await resp.json()) as { tools?: UETool[] };
    const tools = data.tools || [];
    toolCache = { tools, timestamp: Date.now() };
    log.info("Fetched tool list", { count: tools.length });
    return tools;
  } catch (error) {
    log.error("Error fetching tool list", { error: (error as Error).message });
    return toolCache.tools;
  }
}

// ---------------------------------------------------------------------------
// Execute a tool on the UE5 server
// ---------------------------------------------------------------------------

async function executeTool(
  toolName: string,
  params: Record<string, unknown>,
): Promise<Record<string, unknown>> {
  try {
    const result = await uePost("/api/tool", { tool: toolName, ...params });
    return result as Record<string, unknown>;
  } catch (error) {
    return {
      success: false,
      error: `Failed to execute tool "${toolName}": ${(error as Error).message}`,
    };
  }
}

// ---------------------------------------------------------------------------
// Create MCP Server
// ---------------------------------------------------------------------------

const server = new McpServer({
  name: "ue5-ultimate-mcp",
  version: "0.1.0",
});

// ---------------------------------------------------------------------------
// Meta-tool: server_status
// ---------------------------------------------------------------------------

server.tool(
  "server_status",
  "Check UE5 connection status, tool count, and server health",
  {},
  async () => {
    const health = await getUEHealth();

    if (!health) {
      return {
        content: [
          {
            type: "text" as const,
            text: safeStringify({
              connected: false,
              port: UE_PORT,
              message:
                "UE5 server is not running. Start Unreal Editor with UE5UltimateMCP plugin enabled.",
            }),
          },
        ],
        isError: true,
      };
    }

    const tools = await fetchToolList();
    const categories: Record<string, number> = {};
    for (const tool of tools) {
      const cat = categorizeTool(tool.name);
      categories[cat] = (categories[cat] || 0) + 1;
    }

    const simpleCount = tools.filter((t) => classifyTool(t.name) === "simple").length;
    const megaCount = tools.filter((t) => classifyTool(t.name) === "mega").length;
    const hiddenCount = tools.filter((t) => classifyTool(t.name) === "hidden").length;

    const contextFiles = listAvailableContextFiles();

    return {
      content: [
        {
          type: "text" as const,
          text: safeStringify({
            connected: true,
            port: UE_PORT,
            mode: health.mode,
            engineVersion: health.engineVersion,
            projectName: health.projectName,
            pluginVersion: health.pluginVersion,
            totalTools: tools.length,
            breakdown: { simple: simpleCount, mega: megaCount, hidden: hiddenCount },
            categories,
            contextFiles: contextFiles.length,
            message: "UE5 server connected. All systems operational.",
          }),
        },
      ],
    };
  },
);

// ---------------------------------------------------------------------------
// Meta-tool: get_ue_context
// ---------------------------------------------------------------------------

server.tool(
  "get_ue_context",
  `Load UE5 API context documentation. Categories: ${listCategories().join(", ")}`,
  {
    category: z.string().optional().describe(`Specific category: ${listCategories().join(", ")}`),
    query: z.string().optional().describe("Search query to find relevant context"),
  },
  async (args) => {
    const { category, query } = args;

    // Specific category requested
    if (category) {
      const content = loadContextForCategory(category);
      if (content) {
        return {
          content: [
            {
              type: "text" as const,
              text: `# UE5 Context: ${category}\n\n${content}`,
            },
          ],
        };
      }
      return {
        content: [
          {
            type: "text" as const,
            text: `Unknown category: "${category}". Available: ${listCategories().join(", ")}`,
          },
        ],
        isError: true,
      };
    }

    // Query-based search
    if (query) {
      const result = getContextForQuery(query);
      if (result) {
        return {
          content: [
            {
              type: "text" as const,
              text: `# UE5 Context: ${result.categories.join(", ")}\n\n${result.content}`,
            },
          ],
        };
      }
      return {
        content: [
          {
            type: "text" as const,
            text: `No context found for query: "${query}". Try: ${listCategories().join(", ")}`,
          },
        ],
      };
    }

    // No args — list all categories
    const categoryList = listCategories()
      .map((cat) => {
        const info = getCategoryInfo(cat);
        const status = info?.hasContent ? "available" : "no docs yet";
        return `- **${cat}** (${status}): ${info?.keywords.slice(0, 5).join(", ")}...`;
      })
      .join("\n");

    return {
      content: [
        {
          type: "text" as const,
          text: `# Available UE5 Context Categories\n\n${categoryList}\n\nUse \`category\` for specific docs or \`query\` to search by keywords.`,
        },
      ],
    };
  },
);

// ---------------------------------------------------------------------------
// Dynamic tool registration
// ---------------------------------------------------------------------------

/**
 * Register all tools from the UE5 server.
 *
 * Simple tools get registered individually with full schemas.
 * Mega tools get collapsed into category routers.
 * Hidden tools get registered with minimal schema (callable but not listed).
 */
async function registerDynamicTools(): Promise<void> {
  const tools = await fetchToolList();
  if (tools.length === 0) {
    log.info("No tools fetched — UE5 server may not be running. Tools will be registered on first list_tools call.");
    return;
  }

  // Register simple tools individually
  for (const tool of tools) {
    const tier = classifyTool(tool.name);

    if (tier === "simple") {
      const schema = convertToMCPSchema(tool.parameters, true);
      const zodSchema = buildZodFromJsonSchema(schema);

      server.tool(
        tool.name,
        tool.description,
        zodSchema,
        async (args) => {
          const result = await executeTool(tool.name, args as Record<string, unknown>);
          const contextFn = INJECT_CONTEXT ? getContextForTool : undefined;
          return formatToolResponse(tool.name, result as any, contextFn) as any;
        },
      );
    }
  }

  // Register category routers for mega-tools
  const registeredCategories = new Set<string>();
  for (const tool of tools) {
    if (classifyTool(tool.name) !== "mega") continue;
    const catKey = getCategoryForTool(tool.name);
    if (!catKey || registeredCategories.has(catKey)) continue;
    registeredCategories.add(catKey);

    const cat = CATEGORIES[catKey];
    const categoryTools = tools.filter((t) => getCategoryForTool(t.name) === catKey);
    const toolNames = categoryTools.map((t) => t.name);

    server.tool(
      `ue_${catKey}`,
      `${cat.label}: ${cat.description}. Operations: ${toolNames.join(", ")}`,
      {
        operation: z.string().describe(`Tool to invoke. One of: ${toolNames.join(", ")}`),
        params: z.record(z.unknown()).optional().describe("All parameters for the operation"),
      },
      async (args) => {
        const { operation, params: opParams } = args;
        // Validate the operation exists
        if (!toolNames.includes(operation)) {
          return {
            content: [
              {
                type: "text" as const,
                text: `Unknown operation "${operation}" for category "${catKey}". Valid: ${toolNames.join(", ")}`,
              },
            ],
            isError: true,
          };
        }
        const result = await executeTool(operation, (opParams || {}) as Record<string, unknown>);
        const contextFn = INJECT_CONTEXT ? getContextForTool : undefined;
        return formatToolResponse(operation, result as any, contextFn) as any;
      },
    );
  }

  // Register hidden tools (callable but won't clutter the list)
  for (const tool of tools) {
    if (classifyTool(tool.name) !== "hidden") continue;

    server.tool(
      tool.name,
      tool.description,
      { params: z.record(z.unknown()).optional().describe("Tool parameters") },
      async (args) => {
        const result = await executeTool(tool.name, (args.params || {}) as Record<string, unknown>);
        return formatToolResponse(tool.name, result as any) as any;
      },
    );
  }

  log.info("Dynamic tools registered", {
    total: tools.length,
    simple: tools.filter((t) => classifyTool(t.name) === "simple").length,
    megaCategories: registeredCategories.size,
    hidden: tools.filter((t) => classifyTool(t.name) === "hidden").length,
  });
}

// ---------------------------------------------------------------------------
// Zod schema builder from JSON Schema (lightweight)
// ---------------------------------------------------------------------------

/**
 * Build a flat Zod object schema from a JSON Schema properties definition.
 * Handles the common cases from UE5 tool parameters.
 */
function buildZodFromJsonSchema(
  jsonSchema: Record<string, unknown>,
): Record<string, z.ZodType> {
  const properties = (jsonSchema.properties || {}) as Record<string, Record<string, unknown>>;
  const required = new Set((jsonSchema.required || []) as string[]);

  const zodProps: Record<string, z.ZodType> = {};

  for (const [name, prop] of Object.entries(properties)) {
    let zodType: z.ZodType;

    switch (prop.type) {
      case "number":
        zodType = z.number();
        break;
      case "integer":
        zodType = z.number().int();
        break;
      case "boolean":
        zodType = z.boolean();
        break;
      case "array":
        zodType = z.array(z.unknown());
        break;
      case "object":
        zodType = z.record(z.unknown());
        break;
      default:
        zodType = z.string();
    }

    if (prop.description) {
      zodType = zodType.describe(prop.description as string);
    }

    if (!required.has(name)) {
      zodType = zodType.optional();
    }

    zodProps[name] = zodType;
  }

  return zodProps;
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

async function main(): Promise<void> {
  log.info("Starting UE5 Ultimate MCP Server...", { port: UE_PORT });

  // Try to connect to the UE5 server (non-blocking — tools will still register)
  const connectionError = await ensureUE();
  if (connectionError) {
    log.info("UE5 not available at startup — will retry on tool calls", {
      reason: connectionError,
    });
  }

  // Register dynamic tools from the C++ server
  await registerDynamicTools();

  // Set up stdio transport
  const transport = new StdioServerTransport();
  await server.connect(transport);

  // Handle graceful shutdown
  const shutdown = async () => {
    log.info("Shutting down...");
    await gracefulShutdown();
    process.exit(0);
  };

  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);

  const contextFiles = listAvailableContextFiles();
  log.info("UE5 Ultimate MCP Server ready", {
    version: "0.1.0",
    port: UE_PORT,
    toolsCached: toolCache.tools.length,
    contextFiles: contextFiles.length,
    contextCategories: listCategories().length,
    injectContext: INJECT_CONTEXT,
  });
}

main().catch((error) => {
  log.error("Fatal error", { error: (error as Error).message, stack: (error as Error).stack });
  process.exit(1);
});
