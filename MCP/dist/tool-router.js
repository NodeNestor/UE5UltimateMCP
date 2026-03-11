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
// ---------------------------------------------------------------------------
// Classification patterns
// ---------------------------------------------------------------------------
/** Tools that are always listed individually. */
const SIMPLE_PATTERNS = [
    /^spawn_actor$/,
    /^move_actor$/,
    /^delete_actors?$/,
    /^set_property$/,
    /^get_property$/,
    /^get_level_actors$/,
    /^open_level$/,
    /^save_level$/,
    /^asset_search$/,
    /^capture_viewport$/,
    /^get_output_log$/,
    /^run_console_command$/,
    /^execute_python$/,
    /^get_selected_actors$/,
    /^select_actors$/,
];
/** Tools that are callable but never listed (reduces noise). */
const HIDDEN_PATTERNS = [
    /^task_/,
    /^cleanup_scripts$/,
    /^get_script_history$/,
];
export const CATEGORIES = {
    blueprint: {
        label: "Blueprint",
        description: "Blueprint creation, variables, functions, nodes, compilation",
        toolPatterns: [/^blueprint_/, /^bp_/],
    },
    material: {
        label: "Material",
        description: "Materials, material instances, parameters, shader graphs",
        toolPatterns: [/^material_/, /^set_.*material/],
    },
    animation: {
        label: "Animation",
        description: "Animation blueprints, state machines, montages, blendspaces",
        toolPatterns: [/^anim_/, /^animation_/],
    },
    sequencer: {
        label: "Sequencer",
        description: "Level sequences, tracks, keyframes, cinematics",
        toolPatterns: [/^sequencer_/, /^sequence_/],
    },
    landscape: {
        label: "Landscape",
        description: "Landscape creation, sculpting, painting, foliage",
        toolPatterns: [/^landscape_/, /^foliage_/, /^terrain_/],
    },
    niagara: {
        label: "Niagara",
        description: "Particle systems, emitters, modules",
        toolPatterns: [/^niagara_/, /^particle_/],
    },
    ai: {
        label: "AI",
        description: "Behavior trees, blackboards, EQS, AI controllers",
        toolPatterns: [/^ai_/, /^behavior_tree_/, /^blackboard_/, /^eqs_/],
    },
    world: {
        label: "World Building",
        description: "World partition, data layers, level instances, streaming",
        toolPatterns: [/^world_/, /^level_instance_/, /^data_layer_/],
    },
    physics: {
        label: "Physics",
        description: "Physics bodies, constraints, collision, chaos",
        toolPatterns: [/^physics_/, /^collision_/, /^constraint_/],
    },
    audio: {
        label: "Audio",
        description: "Sound cues, MetaSounds, attenuation, audio volumes",
        toolPatterns: [/^audio_/, /^sound_/, /^metasound_/],
    },
    character: {
        label: "Character",
        description: "Character blueprints, movement, input, data assets",
        toolPatterns: [/^character_/, /^enhanced_input_/],
    },
    asset: {
        label: "Asset",
        description: "Asset creation, duplication, import, export, dependencies",
        toolPatterns: [/^asset_(?!search)/, /^import_/, /^export_/],
    },
    umg: {
        label: "UMG / UI",
        description: "Widget blueprints, UMG components, HUD",
        toolPatterns: [/^widget_/, /^umg_/, /^ui_/, /^hud_/],
    },
    pcg: {
        label: "PCG",
        description: "Procedural content generation graphs",
        toolPatterns: [/^pcg_/],
    },
};
// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------
/** Classify a tool into simple / hidden / mega. */
export function classifyTool(toolName) {
    for (const pat of SIMPLE_PATTERNS) {
        if (pat.test(toolName))
            return "simple";
    }
    for (const pat of HIDDEN_PATTERNS) {
        if (pat.test(toolName))
            return "hidden";
    }
    return "mega";
}
/** Find the category key for a mega-tool. Returns null if uncategorized. */
export function getCategoryForTool(toolName) {
    for (const [key, cat] of Object.entries(CATEGORIES)) {
        for (const pat of cat.toolPatterns) {
            if (pat.test(toolName))
                return key;
        }
    }
    return null;
}
/** Group all tools by category for status display. */
export function categorizeTool(toolName) {
    const cat = getCategoryForTool(toolName);
    if (cat)
        return cat;
    if (classifyTool(toolName) === "hidden") {
        return toolName.startsWith("task_") ? "task_queue" : "scripting";
    }
    return "utility";
}
// ---------------------------------------------------------------------------
// Schema helpers
// ---------------------------------------------------------------------------
/** Convert UE tool parameters to a JSON Schema object. */
export function convertToMCPSchema(params, compact = false) {
    const properties = {};
    const required = [];
    for (const param of params || []) {
        const prop = {};
        // Map UE types to JSON Schema types
        const typeMap = {
            number: "number",
            float: "number",
            int: "integer",
            integer: "integer",
            boolean: "boolean",
            bool: "boolean",
            array: "array",
            object: "object",
            string: "string",
        };
        const jsonType = typeMap[param.type] || "string";
        if (param.type !== "any") {
            prop.type = jsonType;
        }
        if (param.description) {
            prop.description =
                compact && param.description.length > 100
                    ? param.description.slice(0, 97) + "..."
                    : param.description;
        }
        if (!compact && param.default !== undefined) {
            prop.default = param.default;
        }
        properties[param.name] = prop;
        if (param.required) {
            required.push(param.name);
        }
    }
    const schema = { type: "object", properties };
    if (required.length > 0)
        schema.required = required;
    return schema;
}
/** Convert UE annotations to MCP tool annotations. */
export function convertAnnotations(annotations) {
    if (!annotations) {
        return {
            readOnlyHint: false,
            destructiveHint: true,
            idempotentHint: false,
            openWorldHint: false,
        };
    }
    return {
        readOnlyHint: annotations.readOnlyHint ?? false,
        destructiveHint: annotations.destructiveHint ?? true,
        idempotentHint: annotations.idempotentHint ?? false,
        openWorldHint: annotations.openWorldHint ?? false,
    };
}
// ---------------------------------------------------------------------------
// Build category router schemas
// ---------------------------------------------------------------------------
/**
 * Build a mega-tool schema for a category, listing all the collapsed tools
 * underneath it so the LLM knows what operations are available.
 */
export function buildCategoryRouterSchema(categoryKey, tools) {
    const cat = CATEGORIES[categoryKey];
    if (!cat)
        return null;
    const toolNames = tools
        .filter((t) => getCategoryForTool(t.name) === categoryKey)
        .map((t) => t.name);
    if (toolNames.length === 0)
        return null;
    return {
        name: `ue_${categoryKey}`,
        description: `${cat.label}: ${cat.description}. Operations: ${toolNames.join(", ")}`,
        inputSchema: {
            type: "object",
            required: ["operation"],
            properties: {
                operation: {
                    type: "string",
                    description: `Tool to invoke. One of: ${toolNames.join(", ")}`,
                },
                params: {
                    type: "object",
                    description: "All parameters for the operation as key-value pairs",
                },
            },
        },
        annotations: {
            readOnlyHint: false,
            destructiveHint: true,
            idempotentHint: false,
            openWorldHint: false,
        },
    };
}
//# sourceMappingURL=tool-router.js.map