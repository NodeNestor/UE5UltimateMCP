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

import { readFileSync, existsSync, readdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

// ---------------------------------------------------------------------------
// Resolve contexts directory relative to this source file
// ---------------------------------------------------------------------------

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
// In dev: src/ -> contexts is at ../contexts
// In dist: dist/ -> contexts is at ../contexts
const CONTEXTS_DIR = join(__dirname, "..", "contexts");

// ---------------------------------------------------------------------------
// Context configuration
// ---------------------------------------------------------------------------

interface ContextEntry {
  files: string[];
  toolPatterns: RegExp[];
  keywords: string[];
}

const CONTEXT_CONFIG: Record<string, ContextEntry> = {
  animation: {
    files: ["animation.md"],
    toolPatterns: [/^anim/, /animation/, /state_machine/, /montage/, /blendspace/],
    keywords: [
      "animation", "anim", "state machine", "blend", "transition",
      "animinstance", "montage", "blendspace", "sequence",
    ],
  },
  blueprint: {
    files: ["blueprint.md"],
    toolPatterns: [/^blueprint/, /^bp_/],
    keywords: [
      "blueprint", "graph", "node", "pin", "uk2node",
      "variable", "function", "event graph", "compile",
    ],
  },
  material: {
    files: ["material.md"],
    toolPatterns: [/^material/, /skeletal_mesh_material/, /actor_material/],
    keywords: [
      "material", "material instance", "scalar parameter", "vector parameter",
      "texture parameter", "roughness", "metallic", "base color", "emissive",
    ],
  },
  actor: {
    files: ["actor.md"],
    toolPatterns: [/spawn/, /actor/, /move/, /delete/, /level/, /open_level/],
    keywords: [
      "actor", "spawn", "component", "transform", "location",
      "rotation", "attach", "destroy", "level", "map",
    ],
  },
  assets: {
    files: ["assets.md"],
    toolPatterns: [/asset/, /import/, /export/, /reference/],
    keywords: [
      "asset", "import", "export", "soft pointer", "async",
      "stream", "reference", "registry", "dependency",
    ],
  },
  sequencer: {
    files: ["sequencer.md"],
    toolPatterns: [/^sequencer/, /^sequence_/],
    keywords: [
      "sequencer", "level sequence", "track", "keyframe",
      "cinematic", "camera cut", "movie render",
    ],
  },
  landscape: {
    files: ["landscape.md"],
    toolPatterns: [/^landscape/, /^foliage/, /^terrain/],
    keywords: [
      "landscape", "terrain", "heightmap", "foliage",
      "sculpt", "paint", "layer",
    ],
  },
  niagara: {
    files: ["niagara.md"],
    toolPatterns: [/^niagara/, /^particle/],
    keywords: [
      "niagara", "particle", "emitter", "module",
      "vfx", "fx", "effect",
    ],
  },
  ai: {
    files: ["ai.md"],
    toolPatterns: [/^ai_/, /^behavior_tree/, /^blackboard/, /^eqs/],
    keywords: [
      "behavior tree", "blackboard", "eqs", "ai controller",
      "navigation", "pathfinding", "perception",
    ],
  },
  character: {
    files: ["character.md"],
    toolPatterns: [/^character/, /character_data/, /movement_param/],
    keywords: [
      "character", "movement", "walk speed", "jump",
      "capsule", "character data", "enhanced input",
    ],
  },
  enhanced_input: {
    files: ["enhanced_input.md"],
    toolPatterns: [/enhanced_input/, /input_action/, /mapping_context/],
    keywords: [
      "enhanced input", "input action", "mapping context",
      "trigger", "modifier", "gamepad", "key binding",
    ],
  },
  physics: {
    files: ["physics.md"],
    toolPatterns: [/^physics/, /^collision/, /^constraint/],
    keywords: [
      "physics", "collision", "constraint", "chaos",
      "rigid body", "simulate", "mass", "gravity",
    ],
  },
  audio: {
    files: ["audio.md"],
    toolPatterns: [/^audio/, /^sound/, /^metasound/],
    keywords: [
      "audio", "sound", "metasound", "attenuation",
      "volume", "cue", "spatial",
    ],
  },
  pcg: {
    files: ["pcg.md"],
    toolPatterns: [/^pcg_/],
    keywords: ["pcg", "procedural", "content generation", "scatter"],
  },
  ue_core: {
    files: ["ue-core-api.md"],
    toolPatterns: [],
    keywords: [
      "uproperty", "ufunction", "uclass", "ustruct",
      "uenum", "specifier", "api reference", "fvector", "ftransform",
    ],
  },
};

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

const contextCache = new Map<string, string>();

function loadContextFile(filename: string): string | null {
  if (contextCache.has(filename)) {
    return contextCache.get(filename)!;
  }

  const filepath = join(CONTEXTS_DIR, filename);
  if (!existsSync(filepath)) {
    return null;
  }

  try {
    const content = readFileSync(filepath, "utf-8");
    contextCache.set(filename, content);
    return content;
  } catch (error) {
    console.error(`[ContextLoader] Error loading ${filename}:`, (error as Error).message);
    return null;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/** Get context category from a tool name. */
export function getCategoryFromTool(toolName: string): string | null {
  const lower = toolName.toLowerCase();
  for (const [category, config] of Object.entries(CONTEXT_CONFIG)) {
    for (const pattern of config.toolPatterns) {
      if (pattern.test(lower)) return category;
    }
  }
  return null;
}

/** Find all matching categories for a freeform query. */
export function getCategoriesFromQuery(query: string): string[] {
  const lower = query.toLowerCase();
  const matches: string[] = [];

  for (const [category, config] of Object.entries(CONTEXT_CONFIG)) {
    for (const keyword of config.keywords) {
      if (lower.includes(keyword.toLowerCase())) {
        if (!matches.includes(category)) {
          matches.push(category);
        }
        break;
      }
    }
  }

  return matches;
}

/** Load combined context content for a category. */
export function loadContextForCategory(category: string): string | null {
  const config = CONTEXT_CONFIG[category];
  if (!config) return null;

  const contents: string[] = [];
  for (const file of config.files) {
    const content = loadContextFile(file);
    if (content) contents.push(content);
  }

  return contents.length > 0 ? contents.join("\n\n---\n\n") : null;
}

/** Get context for a tool call (automatic injection). */
export function getContextForTool(toolName: string): string | null {
  const category = getCategoryFromTool(toolName);
  if (!category) return null;
  return loadContextForCategory(category);
}

/** Get context for a freeform query. */
export function getContextForQuery(
  query: string,
): { categories: string[]; content: string } | null {
  const categories = getCategoriesFromQuery(query);
  if (categories.length === 0) return null;

  const contents: string[] = [];
  for (const cat of categories) {
    const content = loadContextForCategory(cat);
    if (content) contents.push(content);
  }

  if (contents.length === 0) return null;

  return {
    categories,
    content: contents.join("\n\n---\n\n"),
  };
}

/** List all available context categories. */
export function listCategories(): string[] {
  return Object.keys(CONTEXT_CONFIG);
}

/** Get metadata about a category. */
export function getCategoryInfo(category: string): {
  name: string;
  files: string[];
  keywords: string[];
  hasContent: boolean;
} | null {
  const config = CONTEXT_CONFIG[category];
  if (!config) return null;

  // Check if context files actually exist on disk
  const hasContent = config.files.some((f) => existsSync(join(CONTEXTS_DIR, f)));

  return {
    name: category,
    files: config.files,
    keywords: config.keywords,
    hasContent,
  };
}

/** List all context files that actually exist on disk. */
export function listAvailableContextFiles(): string[] {
  try {
    return readdirSync(CONTEXTS_DIR).filter((f) => f.endsWith(".md"));
  } catch {
    return [];
  }
}

/** Clear the file cache (useful for hot-reloading). */
export function clearCache(): void {
  contextCache.clear();
}
