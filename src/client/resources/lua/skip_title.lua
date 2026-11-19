-- Skips the "Press ENTER" title screen and goes straight to the Campaign /
-- Multiplayer / Zombies selection. Does exactly what a button press on that
-- screen does in the game's Main.lua: sign in, then LobbyBeginPlay().
-- Embedded in boiii.exe and loaded after the regular ui_scripts.

if Engine.GetCurrentMap() ~= "core_frontend" then
  return
end

if not LUI.createMenu.Main or not LobbyBeginPlay or not LobbyData or not LobbyData.UITargets.UI_MAIN then
  return
end

local isOnTitleScreen = function()
  local model = Engine.GetModel(Engine.GetGlobalModel(), "lobbyRoot.lobbyNav")
  return model == nil or Engine.GetModelValue(model) == LobbyData.UITargets.UI_MAIN.id
end

-- Returns true once there is nothing left to do for this menu
local tryBeginPlay = function(menu, controller)
  if not isOnTitleScreen() then
    return true
  end

  -- Same conditions as a button press; try again on the next tick
  if Engine.IsCinematicPlaying() or menu.occludedBy then
    return false
  end
  if not Engine.LiveIsControllerSignedIn(controller) and not Engine.SignIntoPlatformLiveSystem(controller) then
    return false
  end

  -- Guests get an error on press; leave that screen to the player
  if Engine.IsUserGuest(controller) then
    return true
  end

  return LobbyBeginPlay(menu, controller) == true
end

local attachSkip = function(menu, controller)
  if menu.boiiiTitleSkipTimer then
    return
  end

  menu.boiiiTitleSkipTimer = LUI.UITimer.newElementTimer(250, false, function()
    if menu.boiiiTitleSkipDone then
      return
    end

    local ok, done = pcall(tryBeginPlay, menu, controller)
    -- On an error stop trying: the title screen still works by hand
    if not ok or done then
      menu.boiiiTitleSkipDone = true
    end
  end)
  menu:addElement(menu.boiiiTitleSkipTimer)
end

-- Title screens created from now on
if not CoD.BoiiiTitleSkipInstalled then
  CoD.BoiiiTitleSkipInstalled = true

  local oldCreateMain = LUI.createMenu.Main
  LUI.createMenu.Main = function(controller, ...)
    local menu = oldCreateMain(controller, ...)
    if menu then
      pcall(attachSkip, menu, controller)
    end
    return menu
  end
end

-- The title screen is usually already open when this script runs
local findMenu
findMenu = function(element, name, depth)
  if element.menuName == name then
    return element
  end
  if depth <= 0 or not element.getFirstChild then
    return nil
  end

  local child = element:getFirstChild()
  while child do
    local found = findMenu(child, name, depth - 1)
    if found then
      return found
    end
    child = child.getNextSibling and child:getNextSibling() or nil
  end
  return nil
end

if LUI.roots and LUI.roots.UIRoot0 then
  local ok, menu = pcall(findMenu, LUI.roots.UIRoot0, "Main", 3)
  if ok and menu then
    local controller = menu.m_ownerController or 0
    pcall(attachSkip, menu, controller)
  end
end
