-- Fixes a bug in the game's ToastNotification widget: a toast shown while
-- another one is still on screen raises "attempt to index a nil value"
-- (toastnotification.lua:39). When the queue is empty, appendNotification
-- stores the new toast as nextNotification and then still does
-- `last.next = toast` with `last` being nil.
-- Embedded in boiii.exe and loaded after the regular ui_scripts.

if not CoD or not CoD.ToastNotification or not CoD.ToastNotification.new then
  return
end

local fixedAppend = function(container, notification)
  if notification == nil then
    return
  end

  local current = container.currentNotification
  if current ~= nil and current.state ~= "NewMusicTrack" and notification.state ~= "NewMusicTrack" then
    -- Queue it behind the toasts already waiting
    if container.nextNotification == nil then
      container.nextNotification = notification
      return
    end

    local last = container.nextNotification
    while last.next ~= nil do
      last = last.next
    end
    last.next = notification
  else
    container:playNotification(notification)
  end
end

local fixWidget = function(widget)
  if widget and widget.ToastContainer and widget.ToastContainer.appendNotification then
    widget.ToastContainer.appendNotification = fixedAppend
  end
end

-- Toast widgets created from now on
if not CoD.BoiiiToastFixInstalled then
  CoD.BoiiiToastFixInstalled = true

  local oldNew = CoD.ToastNotification.new
  CoD.ToastNotification.new = function(...)
    local widget = oldNew(...)
    pcall(fixWidget, widget)
    return widget
  end
end

-- The ones that already exist (frontend, HUD)
local fixExisting
fixExisting = function(element, depth)
  if not element or depth > 8 then
    return
  end

  if element.ToastContainer then
    fixWidget(element)
  end

  if element.getFirstChild then
    local child = element:getFirstChild()
    while child do
      fixExisting(child, depth + 1)
      child = child.getNextSibling and child:getNextSibling() or nil
    end
  end
end

if LUI.roots then
  for _, root in pairs(LUI.roots) do
    if type(root) == "table" then
      pcall(fixExisting, root, 0)
    end
  end
end
