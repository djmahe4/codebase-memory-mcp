/*
 * language.c — Language detection from filename and extension.
 *
 * Maps file extensions and special filenames to CBMLanguage enum values.
 * Handles .m disambiguation (Objective-C vs Magma vs MATLAB).
 * Consults the process-global user config (set via cbm_set_user_lang_config)
 * before the built-in lookup table.
 */
#include "discover/discover.h"
#include "discover/userconfig.h"
#include "cbm.h" // CBMLanguage, CBM_LANG_*

#include "foundation/constants.h"
#include "foundation/compat_fs.h"

enum { LANG_SCAN_PASSES = 2 };
#define SLEN(s) (sizeof(s) - 1)
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ── Extension → Language lookup table ───────────────────────────── */

typedef struct {
    const char *ext; /* including dot, e.g. ".go" */
    CBMLanguage language;
} ext_entry_t;

/* Sorted by extension for binary search (but linear scan is fine for ~120 entries) */
static const ext_entry_t EXT_TABLE[] = {
    {".R", CBM_LANG_R},
    {".S", CBM_LANG_ASSEMBLY},
    {".abnf", CBM_LANG_ABNF},
    {".adb", CBM_LANG_ADA},
    {".ads", CBM_LANG_ADA},
    {".agda", CBM_LANG_AGDA},
    {".astro", CBM_LANG_ASTRO},
    {".awk", CBM_LANG_AWK},
    {".bash", CBM_LANG_BASH},
    {".bass", CBM_LANG_BASS},
    {".bb", CBM_LANG_BITBAKE},
    {".bbappend", CBM_LANG_BITBAKE},
    {".bbclass", CBM_LANG_BITBAKE},
    {".beancount", CBM_LANG_BEANCOUNT},
    {".bib", CBM_LANG_BIBTEX},
    {".bicep", CBM_LANG_BICEP},
    {".blade.php", CBM_LANG_BLADE},
    {".bp", CBM_LANG_BP},
    {".bt", CBM_LANG_BPFTRACE},
    {".bzl", CBM_LANG_STARLARK},
    {".c", CBM_LANG_C},
    {".c3", CBM_LANG_C3},
    {".c3i", CBM_LANG_C3},
    {".cairo", CBM_LANG_CAIRO},
    {".capnp", CBM_LANG_CAPNP},
    {".cbl", CBM_LANG_COBOL},
    {".cc", CBM_LANG_CPP},
    {".ccm", CBM_LANG_CPP},
    {".cedar", CBM_LANG_CEDAR},
    {".cfc", CBM_LANG_CFSCRIPT},
    {".cfg", CBM_LANG_INI},
    {".cfm", CBM_LANG_CFML},
    {".chatito", CBM_LANG_CHATITO},
    {".circom", CBM_LANG_CIRCOM},
    {".cjs", CBM_LANG_JAVASCRIPT},
    {".cl", CBM_LANG_COMMONLISP},
    {".clj", CBM_LANG_CLOJURE},
    {".cljc", CBM_LANG_CLOJURE},
    {".cljs", CBM_LANG_CLOJURE},
    {".cls", CBM_LANG_APEX},
    {".cmake", CBM_LANG_CMAKE},
    {".cob", CBM_LANG_COBOL},
    {".conf", CBM_LANG_INI},
    {".cook", CBM_LANG_COOKLANG},
    {".corn", CBM_LANG_CORN},
    {".cpon", CBM_LANG_CPON},
    {".cpp", CBM_LANG_CPP},
    {".cppm", CBM_LANG_CPP},
    {".cr", CBM_LANG_CRYSTAL},
    {".cs", CBM_LANG_CSHARP},
    {".css", CBM_LANG_CSS},
    {".csv", CBM_LANG_CSV},
    {".cts", CBM_LANG_TYPESCRIPT},
    {".cu", CBM_LANG_CUDA},
    {".cue", CBM_LANG_CUE},
    {".cuh", CBM_LANG_CUDA},
    {".cxx", CBM_LANG_CPP},
    {".cylc", CBM_LANG_CYLC},
    {".d", CBM_LANG_DLANG},
    {".dart", CBM_LANG_DART},
    {".dhall", CBM_LANG_DHALL},
    {".diff", CBM_LANG_DIFF},
    {".dockerfile", CBM_LANG_DOCKERFILE},
    {".dpr", CBM_LANG_PASCAL},
    {".dsp", CBM_LANG_FAUST},
    {".dts", CBM_LANG_DEVICETREE},
    {".dtsi", CBM_LANG_DEVICETREE},
    {".el", CBM_LANG_EMACSLISP},
    {".elm", CBM_LANG_ELM},
    {".elsa", CBM_LANG_ELSA},
    {".elv", CBM_LANG_ELVISH},
    {".env", CBM_LANG_DOTENV},
    {".env.local", CBM_LANG_DOTENV},
    {".erl", CBM_LANG_ERLANG},
    {".ex", CBM_LANG_ELIXIR},
    {".exs", CBM_LANG_ELIXIR},
    {".f03", CBM_LANG_FORTRAN},
    {".f08", CBM_LANG_FORTRAN},
    {".f90", CBM_LANG_FORTRAN},
    {".f95", CBM_LANG_FORTRAN},
    {".facility", CBM_LANG_FACILITY},
    {".fc", CBM_LANG_FUNC},
    {".feature", CBM_LANG_GHERKIN},
    {".fidl", CBM_LANG_FIDL},
    {".fish", CBM_LANG_FISH},
    {".fnl", CBM_LANG_FENNEL},
    {".frag", CBM_LANG_GLSL},
    {".frm", CBM_LANG_FORM},
    {".fs", CBM_LANG_FSHARP},
    {".fsd", CBM_LANG_FACILITY},
    {".fsi", CBM_LANG_FSHARP},
    {".fsx", CBM_LANG_FSHARP},
    {".fx", CBM_LANG_HLSL},
    {".gd", CBM_LANG_GDSCRIPT},
    {".gemspec", CBM_LANG_RUBY},
    {".gitattributes", CBM_LANG_GITATTRIBUTES},
    {".gleam", CBM_LANG_GLEAM},
    {".glsl", CBM_LANG_GLSL},
    {".gn", CBM_LANG_GN},
    {".gni", CBM_LANG_GN},
    {".go", CBM_LANG_GO},
    {".gotmpl", CBM_LANG_GOTEMPLATE},
    {".gql", CBM_LANG_GRAPHQL},
    {".gradle", CBM_LANG_GROOVY},
    {".graphql", CBM_LANG_GRAPHQL},
    {".groovy", CBM_LANG_GROOVY},
    {".h", CBM_LANG_CPP},
    {".ha", CBM_LANG_HARE},
    {".hcl", CBM_LANG_HCL},
    {".hh", CBM_LANG_CPP},
    {".hl", CBM_LANG_HYPRLANG},
    {".hlsl", CBM_LANG_HLSL},
    {".hlsli", CBM_LANG_HLSL},
    {".hpp", CBM_LANG_CPP},
    {".hs", CBM_LANG_HASKELL},
    {".htm", CBM_LANG_HTML},
    {".html", CBM_LANG_HTML},
    {".hx", CBM_LANG_HAXE},
    {".hxx", CBM_LANG_CPP},
    {".inc", CBM_LANG_BITBAKE},
    {".ini", CBM_LANG_INI},
    {".ino", CBM_LANG_ARDUINO},
    {".int", CBM_LANG_OBJECTSCRIPT_ROUTINE},
    {".ispc", CBM_LANG_ISPC},
    {".iuml", CBM_LANG_PLANTUML},
    {".ixx", CBM_LANG_CPP},
    {".j2", CBM_LANG_JINJA2},
    {".janet", CBM_LANG_JANET},
    {".java", CBM_LANG_JAVA},
    {".jinja", CBM_LANG_JINJA2},
    {".jinja2", CBM_LANG_JINJA2},
    {".jl", CBM_LANG_JULIA},
    {".js", CBM_LANG_JAVASCRIPT},
    {".json", CBM_LANG_JSON},
    {".json5", CBM_LANG_JSON5},
    {".jsonnet", CBM_LANG_JSONNET},
    {".jsx", CBM_LANG_JAVASCRIPT},
    {".just", CBM_LANG_JUST},
    {".justfile", CBM_LANG_JUST},
    {".kdl", CBM_LANG_KDL},
    {".kt", CBM_LANG_KOTLIN},
    {".kts", CBM_LANG_KOTLIN},
    {".ld", CBM_LANG_LINKERSCRIPT},
    {".lds", CBM_LANG_LINKERSCRIPT},
    {".lean", CBM_LANG_LEAN},
    {".libsonnet", CBM_LANG_JSONNET},
    {".liquid", CBM_LANG_LIQUID},
    {".lisp", CBM_LANG_COMMONLISP},
    {".ll", CBM_LANG_LLVM_IR},
    {".lpr", CBM_LANG_PASCAL},
    {".lsp", CBM_LANG_COMMONLISP},
    {".lua", CBM_LANG_LUA},
    {".luau", CBM_LANG_LUAU},
    {".m", CBM_LANG_MATLAB},
    {".mac", CBM_LANG_OBJECTSCRIPT_ROUTINE},
    {".mag", CBM_LANG_MAGMA},
    {".magma", CBM_LANG_MAGMA},
    {".matlab", CBM_LANG_MATLAB},
    {".mbt", CBM_LANG_MOONBIT},
    {".md", CBM_LANG_MARKDOWN},
    {".mdx", CBM_LANG_MARKDOWN},
    {".mermaid", CBM_LANG_MERMAID},
    {".meson", CBM_LANG_MESON},
    {".mjs", CBM_LANG_JAVASCRIPT},
    {".mk", CBM_LANG_MAKEFILE},
    {".ml", CBM_LANG_OCAML},
    {".mli", CBM_LANG_OCAML},
    {".mlx", CBM_LANG_MATLAB},
    {".mmd", CBM_LANG_MERMAID},
    {".mo", CBM_LANG_MOTOKO},
    {".mojo", CBM_LANG_MOJO},
    {".move", CBM_LANG_MOVE},
    {".mts", CBM_LANG_TYPESCRIPT},
    {".nasm", CBM_LANG_NASM},
    {".ncl", CBM_LANG_NICKEL},
    {".nim", CBM_LANG_NIM},
    {".nimble", CBM_LANG_NIM},
    {".nims", CBM_LANG_NIM},
    {".nix", CBM_LANG_NIX},
    {".nut", CBM_LANG_SQUIRREL},
    {".odin", CBM_LANG_ODIN},
    {".overlay", CBM_LANG_DEVICETREE},
    {".pas", CBM_LANG_PASCAL},
    {".patch", CBM_LANG_DIFF},
    {".php", CBM_LANG_PHP},
    {".pine", CBM_LANG_PINE},
    {".pkl", CBM_LANG_PKL},
    {".pl", CBM_LANG_PERL},
    {".plantuml", CBM_LANG_PLANTUML},
    {".pm", CBM_LANG_PERL},
    {".pml", CBM_LANG_PROMELA},
    {".po", CBM_LANG_PO},
    {".pony", CBM_LANG_PONY},
    {".pot", CBM_LANG_PO},
    {".pp", CBM_LANG_PUPPET},
    {".prc", CBM_LANG_FORM},
    {".prisma", CBM_LANG_PRISMA},
    {".promela", CBM_LANG_PROMELA},
    {".properties", CBM_LANG_PROPERTIES},
    {".proto", CBM_LANG_PROTOBUF},
    {".ps1", CBM_LANG_POWERSHELL},
    {".psd1", CBM_LANG_POWERSHELL},
    {".psm1", CBM_LANG_POWERSHELL},
    {".puml", CBM_LANG_PLANTUML},
    {".purs", CBM_LANG_PURESCRIPT},
    {".py", CBM_LANG_PYTHON},
    {".qml", CBM_LANG_QML},
    {".r", CBM_LANG_R},
    {".rake", CBM_LANG_RUBY},
    {".rb", CBM_LANG_RUBY},
    {".re", CBM_LANG_REASON},
    {".rego", CBM_LANG_REGO},
    {".rei", CBM_LANG_REASON},
    {".res", CBM_LANG_RESCRIPT},
    {".resi", CBM_LANG_RESCRIPT},
    {".rkt", CBM_LANG_RACKET},
    {".ron", CBM_LANG_RON},
    {".rs", CBM_LANG_RUST},
    {".rst", CBM_LANG_RST},
    {".rtn", CBM_LANG_OBJECTSCRIPT_ROUTINE},
    {".s", CBM_LANG_ASSEMBLY},
    {".sc", CBM_LANG_SCALA},
    {".scala", CBM_LANG_SCALA},
    {".scm", CBM_LANG_SCHEME},
    {".scss", CBM_LANG_SCSS},
    {".sh", CBM_LANG_BASH},
    {".slang", CBM_LANG_SLANG},
    {".slint", CBM_LANG_SLINT},
    {".smali", CBM_LANG_SMALI},
    {".smithy", CBM_LANG_SMITHY},
    {".sol", CBM_LANG_SOLIDITY},
    {".soql", CBM_LANG_SOQL},
    {".sosl", CBM_LANG_SOSL},
    {".sql", CBM_LANG_SQL},
    {".ss", CBM_LANG_SCHEME},
    {".ssh/config", CBM_LANG_SSHCONFIG},
    {".star", CBM_LANG_STARLARK},
    {".sv", CBM_LANG_VERILOG},
    {".svelte", CBM_LANG_SVELTE},
    {".svg", CBM_LANG_XML},
    {".sw", CBM_LANG_SWAY},
    {".swift", CBM_LANG_SWIFT},
    {".tcl", CBM_LANG_TCL},
    {".td", CBM_LANG_TABLEGEN},
    {".templ", CBM_LANG_TEMPL},
    {".tf", CBM_LANG_HCL},
    {".thrift", CBM_LANG_THRIFT},
    {".tl", CBM_LANG_TEAL},
    {".tla", CBM_LANG_TLAPLUS},
    {".tmpl", CBM_LANG_GOTEMPLATE},
    {".toml", CBM_LANG_TOML},
    {".tpl", CBM_LANG_GOTEMPLATE},
    {".tres", CBM_LANG_GODOT_RESOURCE},
    {".trigger", CBM_LANG_APEX},
    {".ts", CBM_LANG_TYPESCRIPT},
    {".tscn", CBM_LANG_GODOT_RESOURCE},
    {".tsx", CBM_LANG_TSX},
    {".typ", CBM_LANG_TYPST},
    {".u", CBM_LANG_UNISON},
    {".v", CBM_LANG_VERILOG},
    {".vala", CBM_LANG_VALA},
    {".vapi", CBM_LANG_VALA},
    {".vert", CBM_LANG_GLSL},
    {".vhd", CBM_LANG_VHDL},
    {".vhdl", CBM_LANG_VHDL},
    {".vim", CBM_LANG_VIMSCRIPT},
    {".vimrc", CBM_LANG_VIMSCRIPT},
    {".vue", CBM_LANG_VUE},
    {".wdl", CBM_LANG_WDL},
    {".wgsl", CBM_LANG_WGSL},
    {".wit", CBM_LANG_WIT},
    {".wl", CBM_LANG_WOLFRAM},
    {".wls", CBM_LANG_WOLFRAM},
    {".wren", CBM_LANG_WREN},
    {".xml", CBM_LANG_XML},
    {".xq", CBM_LANG_XQUERY},
    {".xql", CBM_LANG_XQUERY},
    {".xqm", CBM_LANG_XQUERY},
    {".xquery", CBM_LANG_XQUERY},
    {".xsd", CBM_LANG_XML},
    {".xsl", CBM_LANG_XML},
    {".yaml", CBM_LANG_YAML},
    {".yang", CBM_LANG_YANG},
    {".yml", CBM_LANG_YAML},
    {".yul", CBM_LANG_YUL},
    {".zed", CBM_LANG_AUTHZED},
    {".zig", CBM_LANG_ZIG},
    {".zprofile", CBM_LANG_ZSH},
    {".zsh", CBM_LANG_ZSH},
    {".zshenv", CBM_LANG_ZSH},
    {".zshrc", CBM_LANG_ZSH},
};

#define EXT_TABLE_SIZE (sizeof(EXT_TABLE) / sizeof(EXT_TABLE[0]))

/* ── Special filename → Language lookup ──────────────────────────── */

typedef struct {
    const char *filename;
    CBMLanguage language;
} filename_entry_t;

static const filename_entry_t FILENAME_TABLE[] = {
    {"CMakeLists.txt", CBM_LANG_CMAKE},
    {"Dockerfile", CBM_LANG_DOCKERFILE},
    {"GNUmakefile", CBM_LANG_MAKEFILE},
    {"Makefile", CBM_LANG_MAKEFILE},
    {"makefile", CBM_LANG_MAKEFILE},
    {"meson.build", CBM_LANG_MESON},
    {"meson.options", CBM_LANG_MESON},
    {"meson_options.txt", CBM_LANG_MESON},
    {"kustomization.yaml", CBM_LANG_KUSTOMIZE},
    {"kustomization.yml", CBM_LANG_KUSTOMIZE},
    /* Note: FILENAME_TABLE uses case-sensitive strcmp, so mixed-case variants
     * (e.g. "Kustomization.yaml") are not matched here.  They fall through to
     * CBM_LANG_YAML and are re-classified by cbm_is_kustomize_file() in
     * pass_k8s.c, which performs a case-insensitive comparison.  This is the
     * intended behaviour — no additional entries are needed. */
    {".vimrc", CBM_LANG_VIMSCRIPT},
    {".zshrc", CBM_LANG_ZSH},
    {".zshenv", CBM_LANG_ZSH},
    {".zprofile", CBM_LANG_ZSH},
    {"justfile", CBM_LANG_JUST},
    {"Justfile", CBM_LANG_JUST},
    {".justfile", CBM_LANG_JUST},
    {"hyprland.conf", CBM_LANG_HYPRLANG},
    {"ssh_config", CBM_LANG_SSHCONFIG},
    {"sshd_config", CBM_LANG_SSHCONFIG},
    {".ssh/config", CBM_LANG_SSHCONFIG},
    {"BUILD", CBM_LANG_STARLARK},
    {"BUILD.bazel", CBM_LANG_STARLARK},
    {"WORKSPACE", CBM_LANG_STARLARK},
    {"WORKSPACE.bazel", CBM_LANG_STARLARK},
    {"requirements.txt", CBM_LANG_REQUIREMENTS},
    {"requirements-dev.txt", CBM_LANG_REQUIREMENTS},
    {"requirements-test.txt", CBM_LANG_REQUIREMENTS},
    {"Kconfig", CBM_LANG_KCONFIG},
    {"go.mod", CBM_LANG_GOMOD},
    {".env", CBM_LANG_DOTENV},
    {".env.local", CBM_LANG_DOTENV},
    {".gitattributes", CBM_LANG_GITATTRIBUTES},
};

#define FILENAME_TABLE_SIZE (sizeof(FILENAME_TABLE) / sizeof(FILENAME_TABLE[0]))

/* ── Language names ──────────────────────────────────────────────── */

static const char *LANG_NAMES[CBM_LANG_COUNT] = {
    [CBM_LANG_GO] = "Go",
    [CBM_LANG_PYTHON] = "Python",
    [CBM_LANG_JAVASCRIPT] = "JavaScript",
    [CBM_LANG_TYPESCRIPT] = "TypeScript",
    [CBM_LANG_TSX] = "TSX",
    [CBM_LANG_RUST] = "Rust",
    [CBM_LANG_JAVA] = "Java",
    [CBM_LANG_CPP] = "C++",
    [CBM_LANG_CSHARP] = "C#",
    [CBM_LANG_PHP] = "PHP",
    [CBM_LANG_LUA] = "Lua",
    [CBM_LANG_SCALA] = "Scala",
    [CBM_LANG_KOTLIN] = "Kotlin",
    [CBM_LANG_RUBY] = "Ruby",
    [CBM_LANG_C] = "C",
    [CBM_LANG_BASH] = "Bash",
    [CBM_LANG_ZIG] = "Zig",
    [CBM_LANG_ELIXIR] = "Elixir",
    [CBM_LANG_HASKELL] = "Haskell",
    [CBM_LANG_OCAML] = "OCaml",
    [CBM_LANG_OBJC] = "Objective-C",
    [CBM_LANG_SWIFT] = "Swift",
    [CBM_LANG_DART] = "Dart",
    [CBM_LANG_PERL] = "Perl",
    [CBM_LANG_GROOVY] = "Groovy",
    [CBM_LANG_ERLANG] = "Erlang",
    [CBM_LANG_R] = "R",
    [CBM_LANG_HTML] = "HTML",
    [CBM_LANG_CSS] = "CSS",
    [CBM_LANG_SCSS] = "SCSS",
    [CBM_LANG_YAML] = "YAML",
    [CBM_LANG_TOML] = "TOML",
    [CBM_LANG_HCL] = "HCL",
    [CBM_LANG_SQL] = "SQL",
    [CBM_LANG_DOCKERFILE] = "Dockerfile",
    [CBM_LANG_CLOJURE] = "Clojure",
    [CBM_LANG_FSHARP] = "F#",
    [CBM_LANG_JULIA] = "Julia",
    [CBM_LANG_VIMSCRIPT] = "VimScript",
    [CBM_LANG_NIX] = "Nix",
    [CBM_LANG_COMMONLISP] = "Common Lisp",
    [CBM_LANG_ELM] = "Elm",
    [CBM_LANG_FORTRAN] = "Fortran",
    [CBM_LANG_CUDA] = "CUDA",
    [CBM_LANG_COBOL] = "COBOL",
    [CBM_LANG_VERILOG] = "Verilog",
    [CBM_LANG_EMACSLISP] = "Emacs Lisp",
    [CBM_LANG_JSON] = "JSON",
    [CBM_LANG_XML] = "XML",
    [CBM_LANG_MARKDOWN] = "Markdown",
    [CBM_LANG_MAKEFILE] = "Makefile",
    [CBM_LANG_CMAKE] = "CMake",
    [CBM_LANG_PROTOBUF] = "Protobuf",
    [CBM_LANG_GRAPHQL] = "GraphQL",
    [CBM_LANG_VUE] = "Vue",
    [CBM_LANG_SVELTE] = "Svelte",
    [CBM_LANG_MESON] = "Meson",
    [CBM_LANG_GLSL] = "GLSL",
    [CBM_LANG_INI] = "INI",
    [CBM_LANG_MATLAB] = "MATLAB",
    [CBM_LANG_LEAN] = "Lean",
    [CBM_LANG_FORM] = "FORM",
    [CBM_LANG_MAGMA] = "Magma",
    [CBM_LANG_WOLFRAM] = "Wolfram",
    [CBM_LANG_SOLIDITY] = "Solidity",
    [CBM_LANG_TYPST] = "Typst",
    [CBM_LANG_GDSCRIPT] = "GDScript",
    [CBM_LANG_GLEAM] = "Gleam",
    [CBM_LANG_POWERSHELL] = "PowerShell",
    [CBM_LANG_PASCAL] = "Pascal",
    [CBM_LANG_DLANG] = "D",
    [CBM_LANG_SCHEME] = "Scheme",
    [CBM_LANG_FENNEL] = "Fennel",
    [CBM_LANG_FISH] = "Fish",
    [CBM_LANG_AWK] = "AWK",
    [CBM_LANG_ZSH] = "Zsh",
    [CBM_LANG_TCL] = "Tcl",
    [CBM_LANG_ADA] = "Ada",
    [CBM_LANG_AGDA] = "Agda",
    [CBM_LANG_RACKET] = "Racket",
    [CBM_LANG_ODIN] = "Odin",
    [CBM_LANG_RESCRIPT] = "ReScript",
    [CBM_LANG_PURESCRIPT] = "PureScript",
    [CBM_LANG_NICKEL] = "Nickel",
    [CBM_LANG_CRYSTAL] = "Crystal",
    [CBM_LANG_TEAL] = "Teal",
    [CBM_LANG_HARE] = "Hare",
    [CBM_LANG_PONY] = "Pony",
    [CBM_LANG_LUAU] = "Luau",
    [CBM_LANG_JANET] = "Janet",
    [CBM_LANG_SWAY] = "Sway",
    [CBM_LANG_NASM] = "NASM",
    [CBM_LANG_ASSEMBLY] = "Assembly",
    [CBM_LANG_ASTRO] = "Astro",
    [CBM_LANG_BLADE] = "Blade",
    [CBM_LANG_JUST] = "Just",
    [CBM_LANG_GOTEMPLATE] = "Go Template",
    [CBM_LANG_TEMPL] = "Templ",
    [CBM_LANG_LIQUID] = "Liquid",
    [CBM_LANG_JINJA2] = "Jinja2",
    [CBM_LANG_PRISMA] = "Prisma",
    [CBM_LANG_HYPRLANG] = "Hyprlang",
    [CBM_LANG_DOTENV] = "DotEnv",
    [CBM_LANG_DIFF] = "Diff",
    [CBM_LANG_WGSL] = "WGSL",
    [CBM_LANG_KDL] = "KDL",
    [CBM_LANG_JSON5] = "JSON5",
    [CBM_LANG_JSONNET] = "Jsonnet",
    [CBM_LANG_RON] = "RON",
    [CBM_LANG_THRIFT] = "Thrift",
    [CBM_LANG_CAPNP] = "Cap'n Proto",
    [CBM_LANG_PROPERTIES] = "Properties",
    [CBM_LANG_SSHCONFIG] = "SSH Config",
    [CBM_LANG_BIBTEX] = "BibTeX",
    [CBM_LANG_STARLARK] = "Starlark",
    [CBM_LANG_BICEP] = "Bicep",
    [CBM_LANG_CSV] = "CSV",
    [CBM_LANG_REQUIREMENTS] = "Requirements",
    [CBM_LANG_HLSL] = "HLSL",
    [CBM_LANG_VHDL] = "VHDL",
    [CBM_LANG_SYSTEMVERILOG] = "SystemVerilog",
    [CBM_LANG_DEVICETREE] = "DeviceTree",
    [CBM_LANG_LINKERSCRIPT] = "Linker Script",
    [CBM_LANG_GN] = "GN",
    [CBM_LANG_KCONFIG] = "Kconfig",
    [CBM_LANG_BITBAKE] = "BitBake",
    [CBM_LANG_SMALI] = "Smali",
    [CBM_LANG_TABLEGEN] = "TableGen",
    [CBM_LANG_ISPC] = "ISPC",
    [CBM_LANG_CAIRO] = "Cairo",
    [CBM_LANG_MOVE] = "Move",
    [CBM_LANG_SQUIRREL] = "Squirrel",
    [CBM_LANG_FUNC] = "FunC",
    [CBM_LANG_REGEX] = "Regex",
    [CBM_LANG_JSDOC] = "JSDoc",
    [CBM_LANG_RST] = "reStructuredText",
    [CBM_LANG_BEANCOUNT] = "Beancount",
    [CBM_LANG_MERMAID] = "Mermaid",
    [CBM_LANG_PUPPET] = "Puppet",
    [CBM_LANG_PO] = "PO",
    [CBM_LANG_GITATTRIBUTES] = "gitattributes",
    [CBM_LANG_GITIGNORE] = "gitignore",
    [CBM_LANG_SLANG] = "Slang",
    [CBM_LANG_LLVM_IR] = "LLVM IR",
    [CBM_LANG_SMITHY] = "Smithy",
    [CBM_LANG_WIT] = "WIT",
    [CBM_LANG_TLAPLUS] = "TLA+",
    [CBM_LANG_PKL] = "Pkl",
    [CBM_LANG_GOMOD] = "Go Mod",
    [CBM_LANG_APEX] = "Apex",
    [CBM_LANG_SOQL] = "SOQL",
    [CBM_LANG_SOSL] = "SOSL",
    [CBM_LANG_KUSTOMIZE] = "Kustomize",
    [CBM_LANG_K8S] = "Kubernetes",
    [CBM_LANG_PINE] = "PineScript",
    [CBM_LANG_QML] = "QML",
    [CBM_LANG_CFSCRIPT] = "CFML",
    [CBM_LANG_CFML] = "CFML",
    [CBM_LANG_MOJO] = "Mojo",
    [CBM_LANG_OBJECTSCRIPT_UDL] = "ObjectScript UDL",
    [CBM_LANG_OBJECTSCRIPT_ROUTINE] = "ObjectScript Routine",
    [CBM_LANG_OBJECTSCRIPT_EXPORT] = "ObjectScript Export XML",
    [CBM_LANG_ARDUINO] = "Arduino",
    [CBM_LANG_AUTHZED] = "Authzed",
    [CBM_LANG_BP] = "Blueprint",
    [CBM_LANG_BPFTRACE] = "BPFtrace",
    [CBM_LANG_CHATITO] = "Chatito",
    [CBM_LANG_CORN] = "Corn",
    [CBM_LANG_CPON] = "CPON",
    [CBM_LANG_CUE] = "CUE",
    [CBM_LANG_CYLC] = "Cylc",
    [CBM_LANG_DHALL] = "Dhall",
    [CBM_LANG_GODOT_RESOURCE] = "Godot Resource",
    [CBM_LANG_FACILITY] = "Facility",
    [CBM_LANG_FAUST] = "Faust",
    [CBM_LANG_GHERKIN] = "Gherkin",
    [CBM_LANG_C3] = "C3",
    [CBM_LANG_CIRCOM] = "Circom",
    [CBM_LANG_COOKLANG] = "Cooklang",
    [CBM_LANG_HAXE] = "Haxe",
    [CBM_LANG_NIM] = "Nim",
    [CBM_LANG_PROMELA] = "Promela",
    [CBM_LANG_REASON] = "Reason",
    [CBM_LANG_SLINT] = "Slint",
    [CBM_LANG_UNISON] = "Unison",
    [CBM_LANG_VALA] = "Vala",
    [CBM_LANG_WREN] = "Wren",
    [CBM_LANG_XQUERY] = "XQuery",
    [CBM_LANG_YANG] = "YANG",
    [CBM_LANG_YUL] = "Yul",
    [CBM_LANG_CEDAR] = "Cedar",
    [CBM_LANG_ABNF] = "ABNF",
    [CBM_LANG_MOTOKO] = "Motoko",
    [CBM_LANG_MOONBIT] = "MoonBit",
    [CBM_LANG_WDL] = "WDL",
    [CBM_LANG_REGO] = "Rego",
    [CBM_LANG_PLANTUML] = "PlantUML",
    [CBM_LANG_BASS] = "Bass",
    [CBM_LANG_ELSA] = "Elsa",
    [CBM_LANG_ELVISH] = "Elvish",
    [CBM_LANG_FIDL] = "FIDL",
};

/* ── Public API ──────────────────────────────────────────────────── */

CBMLanguage cbm_language_for_extension(const char *ext) {
    if (!ext || !ext[0]) {
        return CBM_LANG_COUNT;
    }

    /* Check user-defined overrides first */
    const cbm_userconfig_t *ucfg = cbm_get_user_lang_config();
    if (ucfg) {
        CBMLanguage ulang = cbm_userconfig_lookup(ucfg, ext);
        if (ulang != CBM_LANG_COUNT) {
            return ulang;
        }
    }

    for (size_t i = 0; i < EXT_TABLE_SIZE; i++) {
        if (strcmp(EXT_TABLE[i].ext, ext) == 0) {
            return EXT_TABLE[i].language;
        }
    }
    return CBM_LANG_COUNT;
}

CBMLanguage cbm_language_for_filename(const char *filename) {
    if (!filename || !filename[0]) {
        return CBM_LANG_COUNT;
    }

    /* Check special filenames first */
    for (size_t i = 0; i < FILENAME_TABLE_SIZE; i++) {
        if (strcmp(FILENAME_TABLE[i].filename, filename) == 0) {
            return FILENAME_TABLE[i].language;
        }
    }

    /* DotEnv variant filenames (".env.local", ".env.production", …): the
     * filename starts with ".env." but its last "extension" (e.g. ".local")
     * is not a real language extension.  Match the dotenv convention used by
     * pass_envscan/pass_infrascan (".env" exact, ".env." prefix, "*.env"
     * suffix) so file-index routing agrees with direct extraction. */
    if (strncmp(filename, ".env.", SLEN(".env.")) == 0) {
        return CBM_LANG_DOTENV;
    }

    /* Fall back to extension-based lookup.
     * For compound extensions (e.g. ".blade.php") defined in the user config,
     * scan from the first dot in the basename toward the last, checking user
     * config at each position.  Built-in extensions use the last dot only. */
    const char *last_dot = strrchr(filename, '.');
    if (!last_dot) {
        return CBM_LANG_COUNT;
    }

    /* Probe compound extensions (e.g. ".blade.php") from the first dot toward
     * the last. Built-in compounds are checked first so e.g. Laravel Blade
     * templates map to Blade rather than the single-extension fallback (PHP);
     * user config can still add more (#258). */
    static const struct {
        const char *ext;
        CBMLanguage lang;
    } COMPOUND_EXT_TABLE[] = {
        {".blade.php", CBM_LANG_BLADE},
    };
    const cbm_userconfig_t *ucfg = cbm_get_user_lang_config();
    const char *p = strchr(filename, '.');
    while (p && p < last_dot) {
        for (size_t i = 0; i < sizeof(COMPOUND_EXT_TABLE) / sizeof(COMPOUND_EXT_TABLE[0]); i++) {
            if (strcmp(p, COMPOUND_EXT_TABLE[i].ext) == 0) {
                return COMPOUND_EXT_TABLE[i].lang;
            }
        }
        if (ucfg) {
            CBMLanguage lang = cbm_userconfig_lookup(ucfg, p);
            if (lang != CBM_LANG_COUNT) {
                return lang;
            }
        }
        p = strchr(p + SKIP_ONE, '.');
    }

    /* Standard single-extension lookup (built-ins + user overrides). */
    return cbm_language_for_extension(last_dot);
}

const char *cbm_language_name(CBMLanguage lang) {
    if (lang < 0 || lang >= CBM_LANG_COUNT) {
        return "Unknown";
    }
    const char *name = LANG_NAMES[lang];
    return name ? name : "Unknown";
}

/* ── Shebang interpreter detection (extensionless scripts) ────────── */

/* Basename of an interpreter path: the segment after the last '/'.  Shebangs
 * universally use forward slashes even on Windows. */
static const char *interp_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + SKIP_ONE : path;
}

/* "python" optionally followed by an explicit numeric version (digits and dots
 * only, e.g. "python3", "python3.12", "python2.7"). Rejects non-version suffixes
 * like "python-wrapper" or "python3-dbg" to avoid over-matching. */
static bool is_python_interp(const char *base) {
    if (strncmp(base, "python", SLEN("python")) != 0) {
        return false;
    }
    const char *p = base + SLEN("python");
    while (*p) {
        if (!isdigit((unsigned char)*p) && *p != '.') {
            return false;
        }
        p++;
    }
    return true;
}

/* Map an interpreter basename to a language, or CBM_LANG_COUNT if unrecognized.
 * Python version suffixes (python3, python3.12) are handled by is_python_interp. */
static CBMLanguage lang_for_interpreter(const char *base) {
    if (!base || !*base) {
        return CBM_LANG_COUNT;
    }
    if (is_python_interp(base)) {
        return CBM_LANG_PYTHON;
    }
    if (strcmp(base, "sh") == 0 || strcmp(base, "bash") == 0 || strcmp(base, "dash") == 0 ||
        strcmp(base, "ksh") == 0 || strcmp(base, "zsh") == 0) {
        return CBM_LANG_BASH;
    }
    if (strcmp(base, "node") == 0 || strcmp(base, "nodejs") == 0) {
        return CBM_LANG_JAVASCRIPT;
    }
    if (strcmp(base, "ruby") == 0) {
        return CBM_LANG_RUBY;
    }
    if (strcmp(base, "perl") == 0) {
        return CBM_LANG_PERL;
    }
    if (strcmp(base, "php") == 0) {
        return CBM_LANG_PHP;
    }
    if (strcmp(base, "lua") == 0) {
        return CBM_LANG_LUA;
    }
    return CBM_LANG_COUNT;
}

/* Advance *cursor past leading blanks and return the next whitespace-delimited
 * token, NUL-terminating it in place and pointing *cursor past it.
 * Returns NULL when *cursor is exhausted. */
static char *shebang_next_token(char **cursor) {
    char *p = *cursor;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }
    char *start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }
    if (*p != '\0') {
        *p = '\0';
        *cursor = p + SKIP_ONE;
    } else {
        *cursor = p;
    }
    return start;
}

CBMLanguage cbm_language_from_shebang(const char *path) {
    if (!path) {
        return CBM_LANG_COUNT;
    }

    FILE *f = cbm_fopen(path, "rb");
    if (!f) {
        return CBM_LANG_COUNT;
    }

    /* Read enough for a realistic shebang line (<= 256 bytes). */
    char buf[CBM_SZ_256 + SKIP_ONE];
    size_t n = fread(buf, SKIP_ONE, sizeof(buf) - SKIP_ONE, f);
    buf[n] = '\0';

    /* If the buffer filled without finding a newline, check whether the first
     * line actually continues beyond 256 bytes. If so, reject as malformed /
     * binary: real shebangs are well within 256 bytes. Any fread/fgetc error
     * fails closed too. This keeps the read bounded (no unbounded line read
     * or allocation). */
    bool have_newline = (memchr(buf, '\n', n) != NULL);
    if (!have_newline && n == sizeof(buf) - SKIP_ONE) {
        int probe = fgetc(f);
        if (probe != EOF || ferror(f)) {
            (void)fclose(f);
            return CBM_LANG_COUNT;
        }
    }
    (void)fclose(f);

    /* Must begin with "#!". */
    if (n < PAIR_LEN || buf[0] != '#' || buf[1] != '!') {
        return CBM_LANG_COUNT;
    }

    /* Isolate the first line; reject an embedded NUL before the newline. */
    size_t line_len = 0;
    while (line_len < n && buf[line_len] != '\n') {
        if (buf[line_len] == '\0') {
            return CBM_LANG_COUNT; /* embedded NUL — treat as binary */
        }
        line_len++;
    }
    /* Trim a trailing CR so CRLF first lines parse. */
    if (line_len > 0 && buf[line_len - SKIP_ONE] == '\r') {
        line_len--;
    }
    buf[line_len] = '\0';

    /* First token after "#!" is the interpreter (or env). */
    char *cursor = buf + PAIR_LEN;
    char *interp = shebang_next_token(&cursor);
    if (!interp) {
        return CBM_LANG_COUNT;
    }
    const char *base = interp_basename(interp);

    /* "env [-S] <interp> [args...]": the real interpreter is the next token.
     * Only the plain "env <interp>" and "env -S/--split-string <interp> [args]"
     * shapes are supported. After the optional -S, the interpreter token must
     * be a real command, so reject option tokens (leading '-') and NAME=value
     * assignments (containing '=') -- e.g. "env PYTHON=/usr/bin/python
     * python-wrapper", where env would treat the first token as an env-var
     * setting rather than the program to run. */
    if (strcmp(base, "env") == 0) {
        char *tok = shebang_next_token(&cursor);
        if (tok && (strcmp(tok, "-S") == 0 || strcmp(tok, "--split-string") == 0)) {
            tok = shebang_next_token(&cursor);
        }
        if (!tok || tok[0] == '-' || strchr(tok, '=') != NULL) {
            return CBM_LANG_COUNT;
        }
        base = interp_basename(tok);
    }

    return lang_for_interpreter(base);
}

/* ── .m file disambiguation ──────────────────────────────────────── */

/* Simple substring search helper */
static bool str_contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

static bool has_objc_markers(const char *buf) {
    return str_contains(buf, "@interface") || str_contains(buf, "@implementation") ||
           str_contains(buf, "@protocol") || str_contains(buf, "@property") ||
           str_contains(buf, "#import") || str_contains(buf, "@selector") ||
           str_contains(buf, "@encode") || str_contains(buf, "@synthesize") ||
           str_contains(buf, "@dynamic");
}

static bool has_magma_end_markers(const char *buf) {
    return str_contains(buf, "end function;") || str_contains(buf, "end procedure;") ||
           str_contains(buf, "end intrinsic;") || str_contains(buf, "end if;") ||
           str_contains(buf, "end for;") || str_contains(buf, "end while;");
}

/* Check for "intrinsic Name(" or "procedure Name(" patterns. */
static bool has_magma_callable_pattern(const char *buf) {
    const char *markers[] = {"intrinsic ", "procedure "};
    for (int i = 0; i < LANG_SCAN_PASSES; i++) {
        const char *p = strstr(buf, markers[i]);
        if (!p) {
            continue;
        }
        p += strlen(markers[i]);
        while (*p && isalpha((unsigned char)*p)) {
            p++;
        }
        if (*p == '(') {
            return true;
        }
    }
    return false;
}

/* Scan lines for MATLAB-specific markers (function/classdef/%%). */
static bool has_matlab_line_markers(const char *buf) {
    const char *line = buf;
    while (*line) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (strncmp(p, "function ", SLEN("function ")) == 0 ||
            strncmp(p, "function\t", SLEN("function\t")) == 0 ||
            strncmp(p, "classdef ", SLEN("classdef ")) == 0 ||
            strncmp(p, "classdef\t", SLEN("classdef\t")) == 0 || strncmp(p, "%%", PAIR_LEN) == 0 ||
            (*p == '%' && *(p + SKIP_ONE) != '{')) {
            return true;
        }
        const char *nl = strchr(line, '\n');
        if (!nl) {
            break;
        }
        line = nl + SKIP_ONE;
    }
    return false;
}

CBMLanguage cbm_disambiguate_m(const char *path) {
    if (!path) {
        return CBM_LANG_MATLAB;
    }

    FILE *f = cbm_fopen(path, "r");
    if (!f) {
        return CBM_LANG_MATLAB;
    }

    /* Read first 4KB */
    char buf[CBM_SZ_4K + SKIP_ONE];
    size_t n = fread(buf, SKIP_ONE, CBM_SZ_4K, f);
    buf[n] = '\0';
    (void)fclose(f);

    if (has_objc_markers(buf)) {
        return CBM_LANG_OBJC;
    }
    if (has_magma_end_markers(buf)) {
        return CBM_LANG_MAGMA;
    }
    if ((str_contains(buf, "intrinsic ") || str_contains(buf, "procedure ")) &&
        has_magma_callable_pattern(buf)) {
        return CBM_LANG_MAGMA;
    }
    if (has_matlab_line_markers(buf)) {
        return CBM_LANG_MATLAB;
    }

    return CBM_LANG_MATLAB;
}

/* Disambiguate .cls files: shared by InterSystems ObjectScript UDL and
 * Salesforce Apex. ObjectScript class files begin with a line of the form
 * "Class <UppercasePackage>...". Defaults to Apex on any doubt. */
CBMLanguage cbm_disambiguate_cls(const char *path) {
    if (!path) {
        return CBM_LANG_APEX;
    }

    FILE *f = cbm_fopen(path, "r");
    if (!f) {
        return CBM_LANG_APEX;
    }

    char buf[CBM_SZ_4K + SKIP_ONE];
    size_t n = fread(buf, SKIP_ONE, CBM_SZ_4K, f);
    buf[n] = '\0';
    (void)fclose(f);

    const char *line = buf;
    while (*line) {
        if (strncmp(line, "Class ", SLEN("Class ")) == 0 &&
            isupper((unsigned char)line[SLEN("Class ")])) {
            return CBM_LANG_OBJECTSCRIPT_UDL;
        }
        const char *nl = strchr(line, '\n');
        if (!nl) {
            break;
        }
        line = nl + SKIP_ONE;
    }
    return CBM_LANG_APEX;
}

/* Disambiguate .inc files: shared by BitBake include fragments and
 * InterSystems ObjectScript include (macro) files. ObjectScript .inc files are
 * predominantly macro definitions ("#define NAME ..." / "#def1arg NAME ...");
 * some also carry a "ROUTINE <Name>" header. The macro-preprocessor directives
 * are the strongest signal because that is the primary content of an .inc file,
 * whereas BitBake uses '#' only for "# comment" lines (always '#' + space).
 * We therefore match ObjectScript preprocessor directives ('#' immediately
 * followed by 'def'/';'), which BitBake never produces. Defaults to BitBake on
 * any doubt (preserves existing behaviour). */
CBMLanguage cbm_disambiguate_inc(const char *path) {
    if (!path) {
        return CBM_LANG_BITBAKE;
    }

    FILE *f = cbm_fopen(path, "r");
    if (!f) {
        return CBM_LANG_BITBAKE;
    }

    char buf[CBM_SZ_4K + SKIP_ONE];
    size_t n = fread(buf, SKIP_ONE, CBM_SZ_4K, f);
    buf[n] = '\0';
    (void)fclose(f);

    const char *line = buf;
    while (*line) {
        /* ObjectScript include header: a line beginning "ROUTINE <Uppercase>". */
        if (strncmp(line, "ROUTINE ", SLEN("ROUTINE ")) == 0 &&
            isupper((unsigned char)line[SLEN("ROUTINE ")])) {
            return CBM_LANG_OBJECTSCRIPT_ROUTINE;
        }
        /* ObjectScript macro directives — the primary content of .inc files.
         * "#define"/"#def1arg" (macro defs) and "#;" (line comment). BitBake's
         * only '#' use is "# comment" (hash + space), so these never collide. */
        if (strncmp(line, "#define", SLEN("#define")) == 0 ||
            strncmp(line, "#def1arg", SLEN("#def1arg")) == 0 ||
            strncmp(line, "#;", SLEN("#;")) == 0) {
            return CBM_LANG_OBJECTSCRIPT_ROUTINE;
        }
        const char *nl = strchr(line, '\n');
        if (!nl) {
            break;
        }
        line = nl + SKIP_ONE;
    }
    return CBM_LANG_BITBAKE;
}

