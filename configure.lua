---
--- This script runs as a before_build hook for luarocks-build-hooks.
--- After the $(configh) hook has populated rockspec.build.modules.kqueue
--- .configh.report, decide which implementation directory to symlink into
--- ./impl, and expand glob patterns in modules.kqueue.sources so the
--- builtin backend (which does not natively support wildcards) can compile
--- the resulting source list.
---
local rockspec = ...

local kqueue = rockspec and rockspec.build and rockspec.build.modules and
                   rockspec.build.modules.kqueue
assert(kqueue, 'rockspec.build.modules.kqueue not found')

-- The $(configh) hook stores the probe results in kqueue.configh.report.
-- Treat the platform as "supported" only when sys/event.h exists and
-- exposes the kevent function.
local report = kqueue.configh and kqueue.configh.report
local probe = report and report['sys/event.h']
local supported = probe and probe.is_exists and probe.kevent or false

-- create a symbolic link to ./src/ or ./nosup/ as ./impl
local function create_symlink(srcdir)
    os.remove('./impl')
    local cmd = ('ln -sf %s impl'):format(srcdir)
    print('create symlink: ' .. cmd)
    assert(os.execute(cmd))
end
create_symlink(supported and 'src/' or 'nosup/')

-- expand glob patterns in kqueue.sources so the builtin backend can
-- compile them (LuaRocks builtin does not support wildcards natively)
local sources = kqueue.sources
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
