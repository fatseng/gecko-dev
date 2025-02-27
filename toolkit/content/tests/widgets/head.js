"use strict";

function waitForCondition(condition, nextTest, errorMsg) {
  var tries = 0;
  var interval = setInterval(function() {
    if (tries >= 30) {
      ok(false, errorMsg);
      moveOn();
    }
    var conditionPassed;
    try {
      conditionPassed = condition();
    } catch (e) {
      ok(false, e + "\n" + e.stack);
      conditionPassed = false;
    }
    if (conditionPassed) {
      moveOn();
    }
    tries++;
  }, 100);
  var moveOn = function() { clearInterval(interval); nextTest(); };
}

function getAnonElementWithinVideoByAttribute(video, aName, aValue) {
  const videoControl = domUtils.getChildrenForNode(video, true)[1];

  return SpecialPowers.wrap(videoControl.ownerDocument)
    .getAnonymousElementByAttribute(videoControl, aName, aValue);
}
