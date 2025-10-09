include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
set(gwsh_SOURCES
    alias.c
    arith_yacc.c
    arith_yylex.c
    cd.c
    error.c
    eval.c
    exec.c
    expand.c
    histedit.c
    input.c
    jobs.c
    mail.c
    main.c
    memalloc.c
    miscbltin.c
    mylocale.c
    mystring.c
    options.c
    parser.c
    priv.c
    redir.c
    show.c
    signames.c
    trap.c
    output.c
    syntax.c
    system.c
    var.c
    bltin/printf.c
    bltin/test.c
    bltin/times.c)
add_executable(gwsh ${gwsh_SOURCES})
if(LIBEDIT)
  target_link_libraries(gwsh edit)
endif()

add_custom_command(
  OUTPUT token.h token_vars.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS mktokens
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/mktokens
  VERBATIM)
target_sources(gwsh PRIVATE token.h token_vars.h)

add_custom_command(
  OUTPUT builtins.def
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS builtins.def.in
  COMMAND echo \#include \"builtins.def.in\" > builtins.def.c
  COMMAND cpp "-I$<JOIN:$<TARGET_PROPERTY:gwsh,INCLUDE_DIRECTORIES>,;-I>"
          builtins.def.c > builtins.def.tmp
  COMMAND rm builtins.def.c
  COMMAND mv builtins.def.tmp builtins.def
  COMMAND_EXPAND_LISTS VERBATIM)

add_custom_command(
  OUTPUT builtins.c builtins.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS mkbuiltins builtins.def
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/mkbuiltins builtins.def
  VERBATIM)
target_sources(gwsh PRIVATE builtins.c builtins.h)

add_custom_command(
  OUTPUT init.c
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS mkinit.awk
  COMMAND awk -f mkinit.awk ${gwsh_SOURCES} > ${CMAKE_CURRENT_BINARY_DIR}/init.c
  COMMAND_EXPAND_LISTS VERBATIM)
target_sources(gwsh PRIVATE init.c)

add_custom_command(
  OUTPUT nodes.c nodes.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS mknodes.awk nodetypes nodes.c.pat nodes.h.pat
  COMMAND
    awk -f ${CMAKE_CURRENT_SOURCE_DIR}/mknodes.awk
      -v nodes_c_pat=${CMAKE_CURRENT_SOURCE_DIR}/nodes.c.pat -v nodes_c=nodes.c
      -v nodes_h_pat=${CMAKE_CURRENT_SOURCE_DIR}/nodes.h.pat -v nodes_h=nodes.h
      ${CMAKE_CURRENT_SOURCE_DIR}/nodetypes
  VERBATIM)
target_sources(gwsh PRIVATE nodes.c)

install(TARGETS gwsh)
install(FILES gwsh.1 DESTINATION "${CMAKE_INSTALL_FULL_MANDIR}/man1")
