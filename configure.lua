---
--- This script is used to configure to build the project.
--- It is used to check whether the current platform is supported.
---
local configh = require('configh')
local supported = true
local cfgh = configh(os.getenv('CC'))
for header, funcs in pairs({
    ['sys/event.h'] = {
        'kevent',
    },
}) do
    io.stdout:write('check header: ', header, ' ...')
    local ok, err = cfgh:check_header(header)
    if not ok then
        supported = false
        print(' not available')
        print('  >  ' .. string.gsub(err, '\n', '\n  >  '))
    else
        print(' available')
        for _, func in ipairs(funcs) do
            io.stdout:write('check function: ', func, ' ...')
            ok, err = cfgh:check_func(header, func)
            if not ok then
                print(' not available')
                print('  >  ' .. string.gsub(err, '\n', '\n  >  '))
            else
                print(' available')
            end
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
