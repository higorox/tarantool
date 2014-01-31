--[[
 Redistribution and use in source and binary forms, with or
 without modification, are permitted provided that the following
 conditions are met:

 1. Redistributions of source code must retain the above
    copyright notice, this list of conditions and the
    following disclaimer.

 2. Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following
    disclaimer in the documentation and/or other materials
    provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 SUCH DAMAGE.
]]

--[[
    SYNOPSIS

        tap = require 'box.tap'

        tap:plan(10)
        tap:ok(foo(), 'foo returned true')
        tap:is(foo(), 10, 'foo returned 10')
        ...

        tap:check_plan()



    METHODS

        -- service
        :plan(COUNT)                    - set plan of tests
        :check_plan()                   - checks if the plan is done


        -- tests (base)
        :ok(condition, name, ...)       - passed if condition is true
        :fail(name, ...)                - always fail the test
        
        -- tests
        :is(got, expected, name, ...)   - passed if got == expected
        :isnt(got, expected, name, ...) - passed if got ~= expected

        -- checks (lua specific)
        :isnumber(value, name, ...)
        :isstring(value, name, ...)
        :isboolean(value, name, ...)
        :istable(value, name, ...)

        -- diagnostic

        :note(format, ...)      - prints message
        :diag(format, ...)      - prints diagnostic message

]]

local tap = {
    _plan           = nil,
    _done           = 0,
    _failed         = 0,
    _plan_showed    = false,
    _plan_checked   = false,
}


local function sprintf(fmt, ...)
    return string.format(fmt, ...)
end

local function printf(fmt, ...)
    print(sprintf(fmt, ...))
end

setmetatable(tap, {

    __index = {
        ok = function(self, cond, name, ...)
            if not self._plan_showed then
                printf("%d..%d", 1, self._plan or 0)
                self._plan_showed = true
            end
            self._done = self._done + 1
            if cond then
                printf("%s %d - %s", 'ok', self._done, sprintf(name, ...))
            else
                printf("%s %d - %s", 'not ok', self._done, sprintf(name, ...))
                self:diag('%s', debug.traceback())
                self._failed = self._failed + 1
            end
            return cond
        end,

        fail = function(self, name, ...)
            return self:ok(false, name, ...)
        end,

        is = function(self, got, expected, name, ...)
            if self:ok(got == expected, name, ...) then
                return true
            end
            self:diag("     Got: '%s'", tostring(got))
            self:diag("Expected: '%s'", tostring(expected))
            return false
        end,

        isnt = function(self, got, expected, name, ...)
            if self:ok(got ~= expected, name, ...) then
                return true
            end
            self:diag("     Got: '%s'", tostring(got))
            self:diag("Expected: anything else")
            return false
        end,

        isnumber = function(self, v, name, ...)
            return self:is(type(v), 'number', name, ...)
        end,
        
        istable = function(self, v, name, ...)
            return self:is(type(v), 'table', name, ...)
        end,
        
        isstring = function(self, v, name, ...)
            return self:is(type(v), 'string', name, ...)
        end,
        
        isboolean = function(self, v, name, ...)
            return self:is(type(v), 'boolean', name, ...)
        end,

        note = function(self, fmt, ...)
            local str = sprintf(fmt, ...)
            str = "#   " .. string.gsub(str, "\n", "\n#   ")
            print(str)
        end,

        diag = function(self, fmt, ...)
            local str = sprintf(fmt, ...)
            str = "#   " .. string.gsub(str, "\n", "\n#   ")
            print(str)
        end,

        plan = function(self, plan)
            if self._plan ~= nil then
                error("plan is already defined")
            end
            self._plan = plan
        end,

        check_plan = function(self)
            if self._plan_checked then
                return
            end
            if self._failed then
                self:note("Looks like you failed %d test of %d run.",
                    self._failed,
                    self._done
                )
            end
            if self._plan ~= self._done then
                self:note("Looks like you planned %d tests but ran %d.",
                    self._plan or 0,
                    self._done
                )
            end
            return
        end,

        __gc = function(self)
            self:check_plan()
        end
    }
})

return tap
