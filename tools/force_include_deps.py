# PlatformIO pre: script. SCons' include scanner reads #include lines only, so a
# header given with `-include` in build_flags (config.h) is not a dependency:
# editing it alone left stale objects. Every object compiled with a
# force-included header now depends on that header.
import os

Import("env")


def _force_included(build_env):
    paths = []
    for var in ("CCFLAGS", "CFLAGS", "CXXFLAGS"):
        flags = list(build_env.get(var, []))
        for i, flag in enumerate(flags):
            if isinstance(flag, (tuple, list)) and len(flag) == 2 and flag[0] == "-include":
                paths.append(flag[1])
            elif flag == "-include" and i + 1 < len(flags):
                paths.append(flags[i + 1])
    return {os.path.abspath(build_env.subst(str(p)).strip('"')) for p in paths}


def _depend_on_force_includes(build_env, node):
    headers = _force_included(build_env)
    if not headers:
        return node
    objects = build_env.Object(node)
    build_env.Depends(objects, sorted(headers))
    return objects


env.AddBuildMiddleware(_depend_on_force_includes)
