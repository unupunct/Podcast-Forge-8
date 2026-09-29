# Common compile settings for Podcast Forge 8 targets.
function(pf8_set_warnings target)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP /wd4324)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
                                                 _WIN32_WINNT=0x0A00)
endfunction()
