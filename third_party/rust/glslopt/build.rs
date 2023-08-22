use cc;

use std::env;
use std::process::Command;
use std::path::Path;
use std::io;
use std::io::Write;

/// Adds the required definitions to build mesa/glsl-optimizer for the
/// target platform.
fn configure(build: &mut cc::Build) -> &mut cc::Build {
    build.define("__STDC_FORMAT_MACROS", None);
    if cfg!(target_os = "linux") {
        build.define("_GNU_SOURCE", None);
        build.define("HAVE_ENDIAN_H", None);
    }
    if cfg!(any(target_os = "freebsd", target_os = "dragonfly", target_os = "openbsd")) {
        build.define("PTHREAD_SETAFFINITY_IN_NP_HEADER", None);
    }
    if cfg!(target_os = "windows") {
        build.define("_USE_MATH_DEFINES", None);
    } else {
        build.define("HAVE_PTHREAD", None);
        build.define("HAVE_TIMESPEC_GET", None);
    }

    // Avoid using e.g. moz_malloc in Gecko builds.
    build.define("MOZ_INCLUDE_MOZALLOC_H", None);
    // Avoid using e.g. mozalloc_abort in Gecko builds.
    build.define("mozilla_throw_gcc_h", None);

    build
}

fn is_486_target() -> bool {
    let mut is_486_target = false;
    match env::var("TARGET") {
        Ok(val) => is_486_target = val == "i686-unknown-linux-gnu",
        Err(e) => {},
    }
    return is_486_target;
}

fn is_486_sb2() -> bool {
    let mut is_486_sb2 = false;
    match env::var("SB2_TARGET") {
        Ok(val) => is_486_sb2 = val == "i686-unknown-linux-gnu",
        Err(e) => {},
    }
    return is_486_sb2;
}

fn compile(tool: &str, includes: &[&str], file: &str) {
    let out_dir = env::var("OUT_DIR").unwrap();
    let path = Path::new(file);
    let path_out = format!("{}/{}", out_dir,
        &path.parent().unwrap().to_str().unwrap());
    let file_out = format!("{}/{}.o", path_out,
        &path.file_stem().unwrap().to_str().unwrap());
    let magic_include = format!("{}/../../../../include", out_dir);

    // Ensure we have an output directory
    Command::new("mkdir")
        .arg("-p")
        .arg(&path_out)
        .status()
        .unwrap();

    // Compile file.c into file.o
    let mut command = Command::new(tool);
    command
        .args(&["-isystem", &magic_include, "-O0", "-ffunction-sections", "-fdata-sections", "-fPIC", "-g", "-fno-omit-frame-pointer", "-m32", "-march=i686"]);

    for i in 0..includes.len() {
        command.arg("-I");
        command.arg(includes[i]);
    }

    command
        .args(&["-D__STDC_FORMAT_MACROS", "-DHAVE_ENDIAN_H", "-DHAVE_PTHREAD", "-DHAVE_TIMESPEC_GET"])
        .args(&["-o", &file_out])
        .args(&["-c", file]);

    println!("####################### Compiling: {}", file);
    println!("Build command: {:?}", command);

    let output = command
        .output()
        .expect("Failed to execute compile process");

    println!("Compile status: {}", output.status);
    println!("######## stdout:");
    io::stdout().write_all(&output.stdout).unwrap();
    println!("######## stderr:");
    io::stderr().write_all(&output.stderr).unwrap();
    println!("#######################");

    assert!(output.status.success());
}

fn archive(files: &[&str], archivename: &str) {
    let out_dir = env::var("OUT_DIR").unwrap();

    // Archive it all up
    let mut command = Command::new("ar");
    command
        .args(&["cq", &format!("lib{}.a", archivename)])
        .current_dir(&Path::new(&out_dir));

    for i in 0..files.len() {
        let path = Path::new(files[i]);
        let path_out = format!("{}/{}", out_dir,
            &path.parent().unwrap().to_str().unwrap());
        let object_file = &format!("{}/{}.o", path_out,
            &path.file_stem().unwrap().to_str().unwrap()).clone();

        command.arg(object_file);
    }

    command
        .status()
        .unwrap();

    Command::new("ar")
        .args(&["s", &format!("{}/lib{}.a", out_dir, archivename)]);

}

fn build(tool: &str, includes: &[&str], files: &[&str], archivename: &str) {
    let out_dir = env::var("OUT_DIR").unwrap();

    for i in 0..files.len() {
        compile(tool, includes, files[i]);
    }
    archive(files, archivename);

    // Ensure cargo knows where to find the results
    println!("cargo:rustc-link-search=native={}", out_dir);
    println!("cargo:rustc-link-lib=static={}", archivename);
    // Ensure changes propagate
    for i in 0..files.len() {
        println!("cargo:rerun-if-changed={}", files[i]);
    }
}

fn main() {
    // Comment out to control the override behaviour
    let override_compiler = is_486_target() && !is_486_sb2();
    //let override_compiler = false
    //let override_compiler = true;

    if !override_compiler {
        configure(&mut cc::Build::new())
            .warnings(false)
            .include("glsl-optimizer/include")
            .include("glsl-optimizer/src/mesa")
            .include("glsl-optimizer/src/mapi")
            .include("glsl-optimizer/src/compiler")
            .include("glsl-optimizer/src/compiler/glsl")
            .include("glsl-optimizer/src/gallium/auxiliary")
            .include("glsl-optimizer/src/gallium/include")
            .include("glsl-optimizer/src")
            .include("glsl-optimizer/src/util")
            .file("glsl-optimizer/src/compiler/glsl/glcpp/glcpp-lex.c")
            .file("glsl-optimizer/src/compiler/glsl/glcpp/glcpp-parse.c")
            .file("glsl-optimizer/src/compiler/glsl/glcpp/pp_standalone_scaffolding.c")
            .file("glsl-optimizer/src/compiler/glsl/glcpp/pp.c")
            .file("glsl-optimizer/src/util/blob.c")
            .file("glsl-optimizer/src/util/half_float.c")
            .file("glsl-optimizer/src/util/hash_table.c")
            .file("glsl-optimizer/src/util/mesa-sha1.c")
            .file("glsl-optimizer/src/util/os_misc.c")
            .file("glsl-optimizer/src/util/ralloc.c")
            .file("glsl-optimizer/src/util/set.c")
            .file("glsl-optimizer/src/util/sha1/sha1.c")
            .file("glsl-optimizer/src/util/softfloat.c")
            .file("glsl-optimizer/src/util/string_buffer.c")
            .file("glsl-optimizer/src/util/strtod.c")
            .file("glsl-optimizer/src/util/u_debug.c")
            .compile("glcpp");

        configure(&mut cc::Build::new())
            .warnings(false)
            .include("glsl-optimizer/include")
            .include("glsl-optimizer/src/mesa")
            .include("glsl-optimizer/src/mapi")
            .include("glsl-optimizer/src/compiler")
            .include("glsl-optimizer/src/compiler/glsl")
            .include("glsl-optimizer/src/gallium/auxiliary")
            .include("glsl-optimizer/src/gallium/include")
            .include("glsl-optimizer/src")
            .include("glsl-optimizer/src/util")
            .file("glsl-optimizer/src/mesa/program/dummy_errors.c")
            .file("glsl-optimizer/src/mesa/program/symbol_table.c")
            .file("glsl-optimizer/src/mesa/main/extensions_table.c")
            .file("glsl-optimizer/src/compiler/shader_enums.c")
            .compile("mesa");

        configure(&mut cc::Build::new())
            .cpp(true)
            .warnings(false)
            .include("glsl-optimizer/include")
            .include("glsl-optimizer/src/mesa")
            .include("glsl-optimizer/src/mapi")
            .include("glsl-optimizer/src/compiler")
            .include("glsl-optimizer/src/compiler/glsl")
            .include("glsl-optimizer/src/gallium/auxiliary")
            .include("glsl-optimizer/src/gallium/include")
            .include("glsl-optimizer/src")
            .include("glsl-optimizer/src/util")
            .file("glsl-optimizer/src/compiler/glsl_types.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ast_array_index.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ast_expr.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ast_function.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ast_to_hir.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ast_type.cpp")
            .file("glsl-optimizer/src/compiler/glsl/builtin_functions.cpp")
            .file("glsl-optimizer/src/compiler/glsl/builtin_types.cpp")
            .file("glsl-optimizer/src/compiler/glsl/builtin_variables.cpp")
            .file("glsl-optimizer/src/compiler/glsl/generate_ir.cpp")
            .file("glsl-optimizer/src/compiler/glsl/glsl_lexer.cpp")
            .file("glsl-optimizer/src/compiler/glsl/glsl_optimizer.cpp")
            .file("glsl-optimizer/src/compiler/glsl/glsl_parser_extras.cpp")
            .file("glsl-optimizer/src/compiler/glsl/glsl_parser.cpp")
            .file("glsl-optimizer/src/compiler/glsl/glsl_symbol_table.cpp")
            .file("glsl-optimizer/src/compiler/glsl/hir_field_selection.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_array_refcount.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_basic_block.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_builder.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_clone.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_constant_expression.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_equals.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_expression_flattening.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_function_can_inline.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_function_detect_recursion.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_function.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_hierarchical_visitor.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_hv_accept.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_print_glsl_visitor.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_print_visitor.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_reader.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_rvalue_visitor.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_set_program_inouts.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_unused_structs.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_validate.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir_variable_refcount.cpp")
            .file("glsl-optimizer/src/compiler/glsl/ir.cpp")
            .file("glsl-optimizer/src/compiler/glsl/link_atomics.cpp")
            .file("glsl-optimizer/src/compiler/glsl/link_functions.cpp")
            .file("glsl-optimizer/src/compiler/glsl/link_interface_blocks.cpp")
            .file("glsl-optimizer/src/compiler/glsl/link_uniform_block_active_visitor.cpp")
            .file("glsl-optimizer/src/compiler/glsl/link_uniform_blocks.cpp")
            .file("glsl-optimizer/src/compiler/glsl/link_uniform_initializers.cpp")
            .file("glsl-optimizer/src/compiler/glsl/link_uniforms.cpp")
            .file("glsl-optimizer/src/compiler/glsl/link_varyings.cpp")
            .file("glsl-optimizer/src/compiler/glsl/linker_util.cpp")
            .file("glsl-optimizer/src/compiler/glsl/linker.cpp")
            .file("glsl-optimizer/src/compiler/glsl/loop_analysis.cpp")
            .file("glsl-optimizer/src/compiler/glsl/loop_unroll.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_blend_equation_advanced.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_buffer_access.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_builtins.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_const_arrays_to_uniforms.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_cs_derived.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_discard_flow.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_discard.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_distance.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_if_to_cond_assign.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_instructions.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_int64.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_jumps.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_mat_op_to_vec.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_named_interface_blocks.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_offset_array.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_output_reads.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_packed_varyings.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_packing_builtins.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_precision.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_shared_reference.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_subroutine.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_tess_level.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_texture_projection.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_ubo_reference.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_variable_index_to_cond_assign.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_vec_index_to_cond_assign.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_vec_index_to_swizzle.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_vector_derefs.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_vector_insert.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_vector.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_vertex_id.cpp")
            .file("glsl-optimizer/src/compiler/glsl/lower_xfb_varying.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_algebraic.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_array_splitting.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_conditional_discard.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_constant_folding.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_constant_propagation.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_constant_variable.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_copy_propagation_elements.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_dead_builtin_variables.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_dead_builtin_varyings.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_dead_code_local.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_dead_code.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_dead_functions.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_flatten_nested_if_blocks.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_flip_matrices.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_function_inlining.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_if_simplification.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_minmax.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_rebalance_tree.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_redundant_jumps.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_structure_splitting.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_swizzle.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_tree_grafting.cpp")
            .file("glsl-optimizer/src/compiler/glsl/opt_vectorize.cpp")
            .file("glsl-optimizer/src/compiler/glsl/propagate_invariance.cpp")
            .file("glsl-optimizer/src/compiler/glsl/s_expression.cpp")
            .file("glsl-optimizer/src/compiler/glsl/serialize.cpp")
            .file("glsl-optimizer/src/compiler/glsl/shader_cache.cpp")
            .file("glsl-optimizer/src/compiler/glsl/standalone_scaffolding.cpp")
            .file("glsl-optimizer/src/compiler/glsl/string_to_uint_map.cpp")
            .compile("glsl_optimizer");
    }
    else {
        build("host-cc",
        &[
            "glsl-optimizer/include",
            "glsl-optimizer/src/mesa",
            "glsl-optimizer/src/mapi",
            "glsl-optimizer/src/compiler",
            "glsl-optimizer/src/compiler/glsl",
            "glsl-optimizer/src/gallium/auxiliary",
            "glsl-optimizer/src/gallium/include",
            "glsl-optimizer/src",
            "glsl-optimizer/src/util"
        ],
        &[
            "glsl-optimizer/src/compiler/glsl/glcpp/glcpp-lex.c",
            "glsl-optimizer/src/compiler/glsl/glcpp/glcpp-parse.c",
            "glsl-optimizer/src/compiler/glsl/glcpp/pp_standalone_scaffolding.c",
            "glsl-optimizer/src/compiler/glsl/glcpp/pp.c",
            "glsl-optimizer/src/util/blob.c",
            "glsl-optimizer/src/util/half_float.c",
            "glsl-optimizer/src/util/hash_table.c",
            "glsl-optimizer/src/util/mesa-sha1.c",
            "glsl-optimizer/src/util/os_misc.c",
            "glsl-optimizer/src/util/ralloc.c",
            "glsl-optimizer/src/util/set.c",
            "glsl-optimizer/src/util/sha1/sha1.c",
            "glsl-optimizer/src/util/softfloat.c",
            "glsl-optimizer/src/util/string_buffer.c",
            "glsl-optimizer/src/util/strtod.c",
            "glsl-optimizer/src/util/u_debug.c"
        ],
        "glcpp");

        build("host-cc",
        &[
            "glsl-optimizer/include",
            "glsl-optimizer/src/mesa",
            "glsl-optimizer/src/mapi",
            "glsl-optimizer/src/compiler",
            "glsl-optimizer/src/compiler/glsl",
            "glsl-optimizer/src/gallium/auxiliary",
            "glsl-optimizer/src/gallium/include",
            "glsl-optimizer/src",
            "glsl-optimizer/src/util"
        ],
        &[
            "glsl-optimizer/src/mesa/program/dummy_errors.c",
            "glsl-optimizer/src/mesa/program/symbol_table.c",
            "glsl-optimizer/src/mesa/main/extensions_table.c",
            "glsl-optimizer/src/compiler/shader_enums.c"
        ],
        "mesa");

        build("host-c++",
        &[
            "glsl-optimizer/include",
            "glsl-optimizer/src/mesa",
            "glsl-optimizer/src/mapi",
            "glsl-optimizer/src/compiler",
            "glsl-optimizer/src/compiler/glsl",
            "glsl-optimizer/src/gallium/auxiliary",
            "glsl-optimizer/src/gallium/include",
            "glsl-optimizer/src",
            "glsl-optimizer/src/util"
        ],
        &[
            "glsl-optimizer/src/compiler/glsl_types.cpp",
            "glsl-optimizer/src/compiler/glsl/ast_array_index.cpp",
            "glsl-optimizer/src/compiler/glsl/ast_expr.cpp",
            "glsl-optimizer/src/compiler/glsl/ast_function.cpp",
            "glsl-optimizer/src/compiler/glsl/ast_to_hir.cpp",
            "glsl-optimizer/src/compiler/glsl/ast_type.cpp",
            "glsl-optimizer/src/compiler/glsl/builtin_functions.cpp",
            "glsl-optimizer/src/compiler/glsl/builtin_types.cpp",
            "glsl-optimizer/src/compiler/glsl/builtin_variables.cpp",
            "glsl-optimizer/src/compiler/glsl/generate_ir.cpp",
            "glsl-optimizer/src/compiler/glsl/glsl_lexer.cpp",
            "glsl-optimizer/src/compiler/glsl/glsl_optimizer.cpp",
            "glsl-optimizer/src/compiler/glsl/glsl_parser_extras.cpp",
            "glsl-optimizer/src/compiler/glsl/glsl_parser.cpp",
            "glsl-optimizer/src/compiler/glsl/glsl_symbol_table.cpp",
            "glsl-optimizer/src/compiler/glsl/hir_field_selection.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_array_refcount.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_basic_block.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_builder.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_clone.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_constant_expression.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_equals.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_expression_flattening.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_function_can_inline.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_function_detect_recursion.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_function.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_hierarchical_visitor.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_hv_accept.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_print_glsl_visitor.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_print_visitor.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_reader.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_rvalue_visitor.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_set_program_inouts.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_unused_structs.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_validate.cpp",
            "glsl-optimizer/src/compiler/glsl/ir_variable_refcount.cpp",
            "glsl-optimizer/src/compiler/glsl/ir.cpp",
            "glsl-optimizer/src/compiler/glsl/link_atomics.cpp",
            "glsl-optimizer/src/compiler/glsl/link_functions.cpp",
            "glsl-optimizer/src/compiler/glsl/link_interface_blocks.cpp",
            "glsl-optimizer/src/compiler/glsl/link_uniform_block_active_visitor.cpp",
            "glsl-optimizer/src/compiler/glsl/link_uniform_blocks.cpp",
            "glsl-optimizer/src/compiler/glsl/link_uniform_initializers.cpp",
            "glsl-optimizer/src/compiler/glsl/link_uniforms.cpp",
            "glsl-optimizer/src/compiler/glsl/link_varyings.cpp",
            "glsl-optimizer/src/compiler/glsl/linker_util.cpp",
            "glsl-optimizer/src/compiler/glsl/linker.cpp",
            "glsl-optimizer/src/compiler/glsl/loop_analysis.cpp",
            "glsl-optimizer/src/compiler/glsl/loop_unroll.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_blend_equation_advanced.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_buffer_access.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_builtins.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_const_arrays_to_uniforms.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_cs_derived.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_discard_flow.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_discard.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_distance.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_if_to_cond_assign.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_instructions.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_int64.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_jumps.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_mat_op_to_vec.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_named_interface_blocks.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_offset_array.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_output_reads.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_packed_varyings.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_packing_builtins.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_precision.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_shared_reference.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_subroutine.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_tess_level.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_texture_projection.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_ubo_reference.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_variable_index_to_cond_assign.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_vec_index_to_cond_assign.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_vec_index_to_swizzle.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_vector_derefs.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_vector_insert.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_vector.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_vertex_id.cpp",
            "glsl-optimizer/src/compiler/glsl/lower_xfb_varying.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_algebraic.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_array_splitting.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_conditional_discard.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_constant_folding.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_constant_propagation.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_constant_variable.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_copy_propagation_elements.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_dead_builtin_variables.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_dead_builtin_varyings.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_dead_code_local.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_dead_code.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_dead_functions.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_flatten_nested_if_blocks.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_flip_matrices.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_function_inlining.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_if_simplification.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_minmax.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_rebalance_tree.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_redundant_jumps.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_structure_splitting.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_swizzle.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_tree_grafting.cpp",
            "glsl-optimizer/src/compiler/glsl/opt_vectorize.cpp",
            "glsl-optimizer/src/compiler/glsl/propagate_invariance.cpp",
            "glsl-optimizer/src/compiler/glsl/s_expression.cpp",
            "glsl-optimizer/src/compiler/glsl/serialize.cpp",
            "glsl-optimizer/src/compiler/glsl/shader_cache.cpp",
            "glsl-optimizer/src/compiler/glsl/standalone_scaffolding.cpp",
            "glsl-optimizer/src/compiler/glsl/string_to_uint_map.cpp"
        ],
        "glsl_optimizer");

        println!("cargo:rustc-link-lib=stdc++");
    }


}
