-- Built-in "Connect" menu: lists the other BOIII instances running on this
-- machine or the local network (found by the local_instances component) and
-- connects to them.
-- Embedded in boiii.exe and loaded after the regular ui_scripts.

if Engine.GetCurrentMap() ~= "core_frontend" then
  return
end

if not CoD.LobbyButtons or not CoD.LobbyMenus or not game or not game.getlocalinstancecount then
  return
end

local getInstances = function()
  if game.getlocalinstances then
    local instances = {}
    for _, inst in ipairs(game.getlocalinstances()) do
      if inst and inst.address then
        table.insert(instances, inst)
      end
    end
    return instances
  end

  local instances = {}
  for i = 1, game.getlocalinstancecount() do
    local inst = game.getlocalinstance(i)
    if inst and inst.address then
      table.insert(instances, inst)
    end
  end
  return instances
end

-- False until the menu has been open long enough for the first scan to answer
local searchDone = false

-- "NAME (LAN) - ZM 2/4" in a match, "NAME - IN MENU" otherwise. Joining a
-- player who is still in the menu waits until their match starts.
local getInstanceText = function(inst)
  local text = string.upper(inst.name)
  if inst.lan then
    text = text .. " (LAN)"
  end

  if not inst.hosting then
    return text .. " - IN MENU"
  end

  local status = inst.mode or ""
  if inst.maxclients and inst.maxclients > 0 then
    status = status .. " " .. tostring(inst.clients or 0) .. "/" .. tostring(inst.maxclients)
  elseif status == "" then
    status = "IN GAME"
  end
  return text .. " - " .. status
end

DataSources.ConnectMenuButtons = DataSourceHelpers.ListSetup("ConnectMenuButtons", function(controller)
  local listData = {}

  for i, inst in ipairs(getInstances()) do
    local text = getInstanceText(inst)

    local address = inst.address
    table.insert(listData, {
      models = { displayText = text, customId = "btnConnectInstance" .. i },
      properties = {
        title = text,
        action = function(self, element, controller, param, menu)
          -- Close the connect menu first: the join (toasts, lobby, loading)
          -- happens behind it otherwise
          pcall(GoBack, menu or self, controller)
          Engine.Exec(controller, "connect " .. address)
        end,
      },
    })
  end

  if #listData == 0 then
    local text = searchDone and "NO OTHER INSTANCES FOUND" or "SEARCHING..."
    table.insert(listData, {
      models = { displayText = text, customId = "btnConnectNone" },
      properties = {
        title = text,
        action = function(self, element, controller, param, menu) end,
      },
    })
  end

  return listData
end, nil, nil, nil)

LUI.createMenu.ConnectSubMenu = function(controller)
  local self = CoD.Menu.NewForUIEditor("ConnectSubMenu")
  self.soundSet = "default"
  self:setOwner(controller)
  self:setLeftRight(true, true, 0, 0)
  self:setTopBottom(true, true, 0, 0)
  self:playSound("menu_open", controller)
  self.anyChildUsesUpdateState = true

  local Options = LUI.UIList.new(self, controller, 2, 0, nil, false, false, 0, 0, false, false)
  Options:makeFocusable()

  if Engine.IsZombiesGame() then
    Options:setLeftRight(true, false, 314, 594)
    Options:setTopBottom(true, false, 300, 550)
    Options:setYRot(0)
  else
    Options:setLeftRight(true, false, 193.43, 473.43)
    Options:setTopBottom(true, false, 327.56, 650.00)
    Options:setYRot(25)
  end

  -- Ask right away instead of waiting for the next periodic scan
  searchDone = false
  if game.refreshlocalinstances then
    game.refreshlocalinstances()
  end

  Options:setWidgetType(CoD.FE_List1ButtonLarge_PH)
  Options:setVerticalCount(6)
  Options:setSpacing(2)
  Options:setDataSource("ConnectMenuButtons")

  -- Keep the list current while the menu is open: instances come and go, and
  -- their status changes when they start or leave a match. Only rebuild when
  -- something visible changed so the focus doesn't jump.
  local getListSignature = function()
    local parts = { tostring(searchDone) }
    for _, inst in ipairs(getInstances()) do
      table.insert(parts, inst.address .. "|" .. getInstanceText(inst))
    end
    return table.concat(parts, ";")
  end

  -- The client only scans while this menu asks it to
  local lastSignature = getListSignature()
  local refreshTimer = LUI.UITimer.newElementTimer(1000, false, function()
    searchDone = true
    if game.refreshlocalinstances then
      game.refreshlocalinstances()
    end
    local signature = getListSignature()
    if signature ~= lastSignature then
      lastSignature = signature
      pcall(function()
        Options:updateDataSource(false, false)
      end)
    end
  end)
  self:addElement(refreshTimer)

  Options:registerEventHandler("gain_focus", function(element, event)
    local ret = nil
    if element.gainFocus then
      ret = element:gainFocus(event)
    elseif element.super.gainFocus then
      ret = element.super:gainFocus(event)
    end
    CoD.Menu.UpdateButtonShownState(element, self, controller, Enum.LUIButton.LUI_KEY_XBA_PSCROSS)
    return ret
  end)

  self:AddButtonCallbackFunction(Options, controller, Enum.LUIButton.LUI_KEY_XBA_PSCROSS, "ENTER",
    function(element, menu, controller, model)
      ProcessListAction(self, element, controller)
      return true
    end,
    function(element, menu, controller)
      CoD.Menu.SetButtonLabel(menu, Enum.LUIButton.LUI_KEY_XBA_PSCROSS, "MENU_SELECT")
      return true
    end, false)

  self:AddButtonCallbackFunction(self, controller, Enum.LUIButton.LUI_KEY_XBB_PSCIRCLE, nil,
    function(element, menu, controller, model)
      GoBack(self, controller)
      return true
    end,
    function(element, menu, controller)
      CoD.Menu.SetButtonLabel(menu, Enum.LUIButton.LUI_KEY_XBB_PSCIRCLE, "")
      return false
    end, false)

  self:addElement(Options)
  self.Options = Options
  Options.id = "Options"

  self:processEvent({ name = "menu_loaded", controller = controller })
  self:processEvent({ name = "update_state", menu = self })

  if not self:restoreState() then
    self.Options:processEvent({ name = "gain_focus", controller = controller })
  end

  return self
end

CoD.LobbyButtons.CONNECT_MENU = {
  stringRef = "CONNECT",
  action = function(self, element, controller, param, menu)
    SetPerControllerTableProperty(controller, "disableGameSettingsOptions", true)
    OpenPopup(menu, "ConnectSubMenu", controller)
  end,
  customId = "btnConnectMenu",
}

local addConnectButton = function(buttonTable)
  for _, item in ipairs(buttonTable) do
    if item.customId == "btnConnectMenu" then
      return
    end
  end

  table.insert(buttonTable, 1, {
    optionDisplay = CoD.LobbyButtons.CONNECT_MENU.stringRef,
    action = CoD.LobbyButtons.CONNECT_MENU.action,
    customId = CoD.LobbyButtons.CONNECT_MENU.customId,
    isLargeButton = true,
    isLastButtonInGroup = false,
    disabled = false,
    selected = false,
    warning = false,
  })
end

-- Wrap only once per Lua VM, even if this script is loaded again
if not CoD.LobbyMenus.BoiiiConnectMenuInstalled then
  CoD.LobbyMenus.BoiiiConnectMenuInstalled = true

  local oldAddButtonsForTarget = CoD.LobbyMenus.AddButtonsForTarget
  CoD.LobbyMenus.AddButtonsForTarget = function(controller, id)
    local result = oldAddButtonsForTarget(controller, id)
    if
      result
      and (id == LobbyData.UITargets.UI_MPLOBBYMAIN.id or id == LobbyData.UITargets.UI_ZMLOBBYONLINE.id)
    then
      addConnectButton(result)
    end
    return result
  end
end
