#[=======================================================================[.rst:
ProxyBackends
-------------

Provides functions to automatically discover available Proxy backends
and generate handler executables for each backend.

Functions:
  proxy_discover_backends(<dir> [TARGET_OUT_VAR <out_var>] [NAME_OUT_VAR <out_var>])
    Scans subdirectories in <dir> containing CMakeLists.txt and registers/adds them.

  proxy_add_handler_all_backends(<handler_base_name> SOURCES ... [INCLUDE_DIRS ...])
    For each registered backend, creates an executable named:
      <handler_base_name>_<backend_name>
    linking core, the specific backend library, and setting output properties.

  proxy_export_targets_manifest(<output_file>)
    Writes a JSON manifest of all registered handler executable target names to <output_file>,
    including formatting tailored for GitLab CI dynamic child pipelines, as well as
    generating child-pipeline.yml in the same directory.
#]=======================================================================]

# Global property tracking registered backend targets and names
define_property(GLOBAL PROPERTY PROXY_BACKEND_TARGETS
    BRIEF_DOCS "List of registered backend CMake targets"
    FULL_DOCS "List of registered backend CMake targets"
)
define_property(GLOBAL PROPERTY PROXY_BACKEND_NAMES
    BRIEF_DOCS "List of registered backend short names"
    FULL_DOCS "List of registered backend short names"
)
define_property(GLOBAL PROPERTY PROXY_HANDLER_TARGETS
    BRIEF_DOCS "List of generated handler executable targets"
    FULL_DOCS "List of generated handler executable targets"
)

function(proxy_register_backend target_name backend_name)
  set_property(GLOBAL APPEND PROPERTY PROXY_BACKEND_TARGETS "${target_name}")
  set_property(GLOBAL APPEND PROPERTY PROXY_BACKEND_NAMES "${backend_name}")
endfunction()

function(proxy_discover_backends backend_root_dir)
  file(GLOB entries RELATIVE "${backend_root_dir}" "${backend_root_dir}/*")
  foreach(entry IN LISTS entries)
    if(entry STREQUAL "interface")
      continue()
    endif()
    set(entry_path "${backend_root_dir}/${entry}")
    if(IS_DIRECTORY "${entry_path}" AND EXISTS "${entry_path}/CMakeLists.txt")
      add_subdirectory("${entry_path}")
      # Target follows convention backend-<name>
      set(expected_target "backend-${entry}")
      if(TARGET "${expected_target}")
        proxy_register_backend("${expected_target}" "${entry}")
      endif()
    endif()
  endforeach()
endfunction()

function(proxy_add_handler_all_backends handler_base_name)
  cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDE_DIRS" ${ARGN})

  get_property(backend_targets GLOBAL PROPERTY PROXY_BACKEND_TARGETS)
  get_property(backend_names GLOBAL PROPERTY PROXY_BACKEND_NAMES)

  list(LENGTH backend_targets num_backends)
  if(num_backends EQUAL 0)
    message(FATAL_ERROR "No backends registered before defining handler '${handler_base_name}'!")
  endif()

  math(EXPR last_idx "${num_backends} - 1")
  foreach(idx RANGE ${last_idx})
    list(GET backend_targets ${idx} backend_target)
    list(GET backend_names ${idx} backend_name)

    set(target_exec "${handler_base_name}_${backend_name}")

    add_executable(${target_exec} ${ARG_SOURCES})

    if(ARG_INCLUDE_DIRS)
      target_include_directories(${target_exec} PRIVATE ${ARG_INCLUDE_DIRS})
    endif()

    target_link_libraries(${target_exec} PRIVATE core ${backend_target})
    target_compile_features(${target_exec} PRIVATE cxx_std_20)

    # Output binary named with suffix in bin folder
    set_target_properties(${target_exec} PROPERTIES
            OUTPUT_NAME "${handler_base_name}_${backend_name}"
        )

    # Register generated handler target
    set_property(GLOBAL APPEND PROPERTY PROXY_HANDLER_TARGETS "${target_exec}")
  endforeach()
endfunction()

function(proxy_export_targets_manifest output_file)
  get_property(handler_targets GLOBAL PROPERTY PROXY_HANDLER_TARGETS)

  # Format JSON array of target names
  set(json_items "")
  foreach(target IN LISTS handler_targets)
    if(json_items STREQUAL "")
      set(json_items "    \"${target}\"")
    else()
      string(APPEND json_items ",\n    \"${target}\"")
    endif()
  endforeach()

  # Format GitLab CI matrix items: [{"TARGET": "print_handler_file"}, ...]
  set(gitlab_items "")
  foreach(target IN LISTS handler_targets)
    if(gitlab_items STREQUAL "")
      set(gitlab_items "      {\"TARGET\": \"${target}\"}")
    else()
      string(APPEND gitlab_items ",\n      {\"TARGET\": \"${target}\"}")
    endif()
  endforeach()

  # Output tailored manifest JSON (with targets, gitlab_matrix, and parallel.matrix)
  set(manifest_content "{\n  \"targets\": [\n${json_items}\n  ],\n  \"gitlab_matrix\": [\n${gitlab_items}\n  ],\n  \"parallel\": {\n    \"matrix\": [\n${gitlab_items}\n    ]\n  }\n}\n")
  file(WRITE "${output_file}" "${manifest_content}")

  get_filename_component(output_dir "${output_file}" DIRECTORY)

  # Always ensure both manifest.json and targets.json exist in output directory
  file(WRITE "${output_dir}/manifest.json" "${manifest_content}")
  file(WRITE "${output_dir}/targets.json" "${manifest_content}")

  # Also write dynamic child pipeline YAML directly for GitLab CI
  set(yaml_matrix_items "")
  foreach(target IN LISTS handler_targets)
    string(APPEND yaml_matrix_items "      - TARGET: ${target}\n")
  endforeach()

  file(WRITE "${output_dir}/child-pipeline.yml"
"stages:
  - package

package-oci:
  stage: package
  image: docker:24.0.5
  services:
    - docker:24.0.5-dind
  variables:
    DOCKER_TLS_CERTDIR: \"/certs\"
  before_script:
    - echo \"$CI_REGISTRY_PASSWORD\" | docker login -u \"$CI_REGISTRY_USER\" --password-stdin \"$CI_REGISTRY\"
  parallel:
    matrix:
${yaml_matrix_items}  script:
    - docker build --target runtime --build-arg TARGET=\${TARGET} -t \"\${CI_REGISTRY_IMAGE}/\${TARGET}:\${CI_COMMIT_REF_SLUG}\" -t \"\${CI_REGISTRY_IMAGE}/\${TARGET}:latest\" .
    - docker push \"\${CI_REGISTRY_IMAGE}/\${TARGET}:\${CI_COMMIT_REF_SLUG}\"
    - docker push \"\${CI_REGISTRY_IMAGE}/\${TARGET}:latest\"
")
endfunction()
