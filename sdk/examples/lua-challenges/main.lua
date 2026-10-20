-- Challenges: an example Lua script mod for the Super Mario Sunshine PC port.
--
-- Each time a stage loads it sets a challenge (challenges.lua) against the
-- clock, shows it in a panel at the top right, marks targets in the world,
-- plays the game's sounds when it is won or lost, and keeps the best streak
-- in its settings. R (key.reroll in its settings) picks another one.
--
-- It shows the Lua side of the mod API (docs/MODDING.md, "Lua scripts"):
-- events with sms.on, drawing with sms.draw, settings and options for the
-- Mods menu, a key binding, require() for a module of its own, watching
-- damage at MONITOR priority, and reading the C stage-timer mod's custom
-- event with string.unpack.

local challenges = require("challenges")

-- settings (the Mods menu shows the options; F1)
sms.option{ key = "show", label = "Show challenges", type = "bool", default = true }
sms.option{ key = "time_limit", label = "Time limit (seconds)", type = "int",
            default = 45, min = 15, max = 120, step = 15 }
sms.option{ key = "sounds", label = "Sounds", type = "bool", default = true }
sms.option{ key = "reset", label = "Clear best streak", type = "action",
            on_change = function() sms.set_setting("best_streak", 0) sms.log("best streak cleared") end }

local reroll_key = sms.bind_key("reroll", "R")

local state = {
  index = nil,    -- which challenge (challenges.list)
  c = nil,        -- its own table
  ticks = 0,      -- ticks since it began
  tps = 30,
  result = nil,   -- nil while playing, "won" or "lost"
  shown_until = 0,
  streak = 0,
  split = nil,    -- the stage-timer mod's last split, when it is loaded
}

local function sound(id)
  if sms.setting("sounds", true) then sms.play_sound(id) end
end

local function begin(index)
  local mario, st = sms.mario(), sms.state()
  -- a stage being played: the title screen's background is a stage too (area 15)
  if not mario or st.mode ~= "stage" or st.area == 15 then return end
  state.index = index or challenges.pick(state.index)
  local def = challenges.list[state.index]
  state.c = { start = mario.position }
  def.start(state.c)
  state.ticks, state.result = 0, nil
  sms.log(("challenge: %s (%s)"):format(def.name, def.describe(state.c)))
  sound(sms.SOUND_NEWS)
end

local function finish(result)
  state.result = result
  state.shown_until = sms.time() + 3
  if result == "won" then
    state.streak = state.streak + 1
    if state.streak > sms.setting("best_streak", 0) then
      sms.set_setting("best_streak", state.streak)
    end
    sound(sms.SOUND_SHINE)
  else
    state.streak = 0
    sound(sms.SOUND_DAMAGE)
  end
  sms.log(("challenge %s; streak %d"):format(result, state.streak))
end

-- ------------------------------------------------------------------ events

sms.on("stage_loaded", function(ev)
  state.index, state.c, state.result = nil, nil, nil
  state.load_tick = 0 -- start once Mario has stood for a moment
end)

sms.on("tick", function(ev)
  state.tps = ev.ticks_per_second
  if state.load_tick then
    state.load_tick = state.load_tick + 1
    if state.load_tick > ev.ticks_per_second * 2 then
      state.load_tick = nil
      begin()
    end
    return
  end
  if not state.c or state.result then return end
  local def, mario = challenges.list[state.index], sms.mario()
  if not mario then return end
  state.ticks = state.ticks + 1
  if def.air and not mario.on_ground then def.air(state.c, ev.dt) end
  local p = def.progress(state.c, mario)
  local limit = sms.setting("time_limit", 45) * ev.ticks_per_second
  if p < 0 then
    finish("lost")
  elseif p >= 1 and not def.survive then
    finish("won")
  elseif state.ticks >= limit then
    finish(def.survive and "won" or "lost")
  end
end)

sms.on("item_collected", function(ev)
  local def = state.c and not state.result and challenges.list[state.index]
  if def and def.coin and ev.kind == "coin" then def.coin(state.c) end
end)

-- MONITOR: only hits that land (another mod, the trainer HUD's invincibility,
-- may have cancelled this one)
sms.on("player_damage", function(ev)
  local def = state.c and not state.result and challenges.list[state.index]
  if def and def.hit and not ev.cancelled then def.hit(state.c) end
end, sms.MONITOR)

-- the reroll key: consumed, so the game's controls never see it
sms.on("key", function(ev)
  if ev.scancode ~= reroll_key then return end
  if ev.down and not ev.repeated and sms.state().stage_ready then begin() end
  return true
end)

-- stage-timer (a C mod) sends {float seconds; int shine} as "stage-timer.split";
-- nothing arrives when it is not loaded
sms.on("stage-timer.split", function(ev)
  if ev.data and #ev.data >= 8 then
    local seconds, shine = string.unpack("<fi4", ev.data)
    state.split = ("Shine %d at %d:%05.2f"):format(shine, seconds // 60, seconds % 60)
  end
end)

-- ------------------------------------------------------------------ drawing

local WHITE, DIM = sms.rgb(255, 255, 255), sms.rgb(170, 180, 200)
local GOLD, GREEN, RED = sms.rgb(250, 190, 40), sms.rgb(120, 230, 140), sms.rgb(255, 110, 100)

sms.on("draw", function(ev)
  local st = sms.state()
  if ev.mode ~= "stage" or st.paused or not state.c or not sms.setting("show", true) then return end
  if state.result and sms.time() > state.shown_until then return end
  local d = sms.draw
  local def = challenges.list[state.index]
  local x, y, w, h = d.hud_slot("top_right", 190, 62)
  if not x then return end

  d.gradient(x, y, w, h, sms.rgba(40, 20, 60, 210), sms.rgba(20, 10, 30, 180))
  d.rect(x + w - 3, y, 3, h, GOLD)
  d.text(x + 8, y + 5, 13, GOLD, def.name, sms.SHADOW)
  d.text(x + 8, y + 21, 10, DIM, def.describe(state.c))

  local mario = sms.mario()
  local p = 0
  if mario and not state.result then p = math.max(0, math.min(1, def.progress(state.c, mario))) end
  local limit = sms.setting("time_limit", 45)
  local left = math.max(0, limit - state.ticks / state.tps)
  if def.survive then p = 1 - left / limit end
  d.rect(x + 8, y + 37, w - 60, 6, sms.rgba(255, 255, 255, 40))
  d.rect(x + 8, y + 37, (w - 60) * (state.result == "won" and 1 or p), 6, state.result == "lost" and RED or GREEN)
  d.text(x + w - 10, y + 34, 12, left < 10 and RED or WHITE, ("%.1f"):format(left), sms.RIGHT)

  local status = state.result == "won" and "COMPLETE!" or state.result == "lost" and "FAILED"
                 or ("streak %d   best %d"):format(state.streak, sms.setting("best_streak", 0))
  d.text(x + 8, y + 48, 10, state.result == "won" and GREEN or state.result == "lost" and RED or DIM, status)

  -- the target in the world, if the challenge has one
  if def.marker and not state.result then
    local m = def.marker(state.c)
    d.world_ring(m, 120, 3, GOLD)
    d.world_text(m, 14, GOLD, "UP HERE", sms.OUTLINE)
  end
  if state.split then
    d.text(x + 8, y + h + 2, 9, GREEN, state.split, sms.SHADOW)
  end
end)

sms.log(("ready (%s): %d challenges, R picks another"):format(sms.lua_version, #challenges.list))
