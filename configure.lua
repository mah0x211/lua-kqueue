---
--- This script is used to configure to build the project.
--- It is used to check whether the current platform is supported.
---
local configh = require('configh')
local supported = true
local cfgh = configh(os.getenv('CC'))
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

-- create symbolic link to src/ directory
local function create_symlink(srcdir)
    os.remove('./impl')
    local cmd = ('ln -sf %s impl'):format(srcdir)
    print('cretate symlink: ' .. cmd)
    assert(os.execute(cmd))
end
create_symlink(supported and 'src/' or 'nosup/')
