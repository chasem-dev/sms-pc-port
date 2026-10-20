-- The challenges: a module main.lua loads with require("challenges").
--
-- Each challenge has a name, a description, and functions the main script
-- calls: start(c) when it begins, and progress(c) each tick, which returns
-- how far along it is, 0 to 1 (1 wins). `c` is the challenge's own table,
-- where it keeps what it needs; c.start is where Mario stood when it began.
-- Events the main script passes on are optional: coin(c), hit(c), air(c, dt).

local M = {}

M.list = {
  {
    name = "Coin rush",
    describe = function(c) return ("Collect %d coins"):format(c.goal) end,
    start = function(c) c.goal, c.coins = 6, 0 end,
    coin = function(c) c.coins = c.coins + 1 end,
    progress = function(c) return c.coins / c.goal end,
  },
  {
    name = "Untouchable",
    -- a survival challenge: winning is lasting until the clock runs out
    survive = true,
    describe = function() return "Take no damage until time runs out" end,
    start = function(c) c.hits = 0 end,
    hit = function(c) c.hits = c.hits + 1 end,
    progress = function(c) return c.hits > 0 and -1 or 0 end, -- -1 fails it
  },
  {
    name = "Skyward",
    describe = function(c) return ("Climb %d units above where you started"):format(c.goal) end,
    start = function(c) c.goal = 600 end,
    progress = function(c, mario) return (mario.position.y - c.start.y) / c.goal end,
    -- a ring at the height to reach, over where it began
    marker = function(c) return { x = c.start.x, y = c.start.y + c.goal, z = c.start.z } end,
  },
  {
    name = "Marathon",
    describe = function(c) return ("Run %d units"):format(c.goal) end,
    start = function(c) c.goal, c.ran, c.last = 4000, 0, nil end,
    progress = function(c, mario)
      local p = mario.position
      if c.last then
        local dx, dz = p.x - c.last.x, p.z - c.last.z
        local d = math.sqrt(dx * dx + dz * dz)
        if d < 200 then c.ran = c.ran + d end -- a warp is not a run
      end
      c.last = { x = p.x, z = p.z }
      return c.ran / c.goal
    end,
  },
  {
    name = "Frequent flyer",
    describe = function(c) return ("Spend %.0f seconds in the air"):format(c.goal) end,
    start = function(c) c.goal, c.flown = 4, 0 end,
    air = function(c, dt) c.flown = c.flown + dt end,
    progress = function(c) return c.flown / c.goal end,
  },
}

-- A challenge to play: any but the last one.
function M.pick(last)
  local i
  repeat i = math.random(#M.list) until i ~= last or #M.list == 1
  return i
end

return M
