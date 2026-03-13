---
--- This script is used as a before_build hook for luarocks-build-hooks.
--- It checks whether the current platform is supported and expands glob
--- patterns in rockspec.build.modules[*].sources so the LuaRocks builtin
--- backend (which does not support wildcards natively) can compile them.
---
local rockspec = ...

local configh = require('configh')
local supported = true
local cc = os.getenv('CC') or
               (rockspec and rockspec.variables and rockspec.variables.CC)
local cfgh = configh(cc)
cfgh:output_status(true)
for header, funcs in pairs({
    ['sys/event.h'] = {
        'kevent',
    },
}) do
    if not cfgh:check_header(header) then
        supported = false
    else
        for _, func in ipairs(funcs) do
            cfgh:check_func(header, func)
        end
    end
end
assert(cfgh:flush('src/config.h'))

-- create symbolic link to src/ or nosup/ directory
local function create_symlink(srcdir)
    os.remove('./impl')
    local cmd = ('ln -sf %s impl'):format(srcdir)
    print('create symlink: ' .. cmd)
    assert(os.execute(cmd))
end
create_symlink(supported and 'src/' or 'nosup/')

-- expand glob patterns in modules.kqueue.sources so the builtin backend
-- can compile them (LuaRocks builtin does not support wildcards natively)
if rockspec and rockspec.build and rockspec.build.modules then
    local kqueue = rockspec.build.modules.kqueue
    local sources = kqueue and kqueue.sources
    if type(sources) == 'string' then
        sources = {
            sources,
        }
    end
    if type(sources) == 'table' then
        local expanded = {}
        for _, src in ipairs(sources) do
            if src:find('[*?]') then
                local pipe = assert(io.popen('ls ' .. src))
                for file in pipe:lines() do
                    expanded[#expanded + 1] = file
                end
                pipe:close()
            else
                expanded[#expanded + 1] = src
            end
        end
        kqueue.sources = expanded
    end
end
