/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* General utilities used throughout devtools. */
const { Ci, Cu } = require("chrome");

let { Services } = Cu.import("resource://gre/modules/Services.jsm", {});
let { setTimeout, clearTimeout } = Cu.import("resource://gre/modules/Timer.jsm", {});

/**
 * Turn the error |aError| into a string, without fail.
 */
exports.safeErrorString = function safeErrorString(aError) {
  try {
    let errorString = aError.toString();
    if (typeof errorString == "string") {
      // Attempt to attach a stack to |errorString|. If it throws an error, or
      // isn't a string, don't use it.
      try {
        if (aError.stack) {
          let stack = aError.stack.toString();
          if (typeof stack == "string") {
            errorString += "\nStack: " + stack;
          }
        }
      } catch (ee) { }

      // Append additional line and column number information to the output,
      // since it might not be part of the stringified error.
      if (typeof aError.lineNumber == "number" && typeof aError.columnNumber == "number") {
        errorString += "Line: " + aError.lineNumber + ", column: " + aError.columnNumber;
      }

      return errorString;
    }
  } catch (ee) { }

  return "<failed trying to find error description>";
}

/**
 * Report that |aWho| threw an exception, |aException|.
 */
exports.reportException = function reportException(aWho, aException) {
  let msg = aWho + " threw an exception: " + exports.safeErrorString(aException);

  dump(msg + "\n");

  if (Cu.reportError) {
    /*
     * Note that the xpcshell test harness registers an observer for
     * console messages, so when we're running tests, this will cause
     * the test to quit.
     */
    Cu.reportError(msg);
  }
}

/**
 * Given a handler function that may throw, return an infallible handler
 * function that calls the fallible handler, and logs any exceptions it
 * throws.
 *
 * @param aHandler function
 *      A handler function, which may throw.
 * @param aName string
 *      A name for aHandler, for use in error messages. If omitted, we use
 *      aHandler.name.
 *
 * (SpiderMonkey does generate good names for anonymous functions, but we
 * don't have a way to get at them from JavaScript at the moment.)
 */
exports.makeInfallible = function makeInfallible(aHandler, aName) {
  if (!aName)
    aName = aHandler.name;

  return function (/* arguments */) {
    try {
      return aHandler.apply(this, arguments);
    } catch (ex) {
      let who = "Handler function";
      if (aName) {
        who += " " + aName;
      }
      exports.reportException(who, ex);
    }
  }
}

/**
 * Interleaves two arrays element by element, returning the combined array, like
 * a zip. In the case of arrays with different sizes, undefined values will be
 * interleaved at the end along with the extra values of the larger array.
 *
 * @param Array a
 * @param Array b
 * @returns Array
 *          The combined array, in the form [a1, b1, a2, b2, ...]
 */
exports.zip = function zip(a, b) {
  if (!b) {
    return a;
  }
  if (!a) {
    return b;
  }
  const pairs = [];
  for (let i = 0, aLength = a.length, bLength = b.length;
       i < aLength || i < bLength;
       i++) {
    pairs.push([a[i], b[i]]);
  }
  return pairs;
};

/**
 * Waits for the next tick in the event loop to execute a callback.
 */
exports.executeSoon = function executeSoon(aFn) {
  Services.tm.mainThread.dispatch({
    run: exports.makeInfallible(aFn)
  }, Ci.nsIThread.DISPATCH_NORMAL);
};

/**
 * Waits for the next tick in the event loop.
 *
 * @return Promise
 *         A promise that is resolved after the next tick in the event loop.
 */
exports.waitForTick = function waitForTick() {
  let deferred = promise.defer();
  exports.executeSoon(deferred.resolve);
  return deferred.promise;
};

/**
 * Waits for the specified amount of time to pass.
 *
 * @param number aDelay
 *        The amount of time to wait, in milliseconds.
 * @return Promise
 *         A promise that is resolved after the specified amount of time passes.
 */
exports.waitForTime = function waitForTime(aDelay) {
  let deferred = promise.defer();
  setTimeout(deferred.resolve, aDelay);
  return deferred.promise;
};

/**
 * Like Array.prototype.forEach, but doesn't cause jankiness when iterating over
 * very large arrays by yielding to the browser and continuing execution on the
 * next tick.
 *
 * @param Array aArray
 *        The array being iterated over.
 * @param Function aFn
 *        The function called on each item in the array. If a promise is
 *        returned by this function, iterating over the array will be paused
 *        until the respective promise is resolved.
 * @returns Promise
 *          A promise that is resolved once the whole array has been iterated
 *          over, and all promises returned by the aFn callback are resolved.
 */
exports.yieldingEach = function yieldingEach(aArray, aFn) {
  const deferred = promise.defer();

  let i = 0;
  let len = aArray.length;
  let outstanding = [deferred.promise];

  (function loop() {
    const start = Date.now();

    while (i < len) {
      // Don't block the main thread for longer than 16 ms at a time. To
      // maintain 60fps, you have to render every frame in at least 16ms; we
      // aren't including time spent in non-JS here, but this is Good
      // Enough(tm).
      if (Date.now() - start > 16) {
        exports.executeSoon(loop);
        return;
      }

      try {
        outstanding.push(aFn(aArray[i], i++));
      } catch (e) {
        deferred.reject(e);
        return;
      }
    }

    deferred.resolve();
  }());

  return promise.all(outstanding);
}

/**
 * Like XPCOMUtils.defineLazyGetter, but with a |this| sensitive getter that
 * allows the lazy getter to be defined on a prototype and work correctly with
 * instances.
 *
 * @param Object aObject
 *        The prototype object to define the lazy getter on.
 * @param String aKey
 *        The key to define the lazy getter on.
 * @param Function aCallback
 *        The callback that will be called to determine the value. Will be
 *        called with the |this| value of the current instance.
 */
exports.defineLazyPrototypeGetter =
function defineLazyPrototypeGetter(aObject, aKey, aCallback) {
  Object.defineProperty(aObject, aKey, {
    configurable: true,
    get: function() {
      const value = aCallback.call(this);

      Object.defineProperty(this, aKey, {
        configurable: true,
        writable: true,
        value: value
      });

      return value;
    }
  });
}

/**
 * Safely get the property value from a Debugger.Object for a given key. Walks
 * the prototype chain until the property is found.
 *
 * @param Debugger.Object aObject
 *        The Debugger.Object to get the value from.
 * @param String aKey
 *        The key to look for.
 * @return Any
 */
exports.getProperty = function getProperty(aObj, aKey) {
  let root = aObj;
  try {
    do {
      const desc = aObj.getOwnPropertyDescriptor(aKey);
      if (desc) {
        if ("value" in desc) {
          return desc.value;
        }
        // Call the getter if it's safe.
        return exports.hasSafeGetter(desc) ? desc.get.call(root).return : undefined;
      }
      aObj = aObj.proto;
    } while (aObj);
  } catch (e) {
    // If anything goes wrong report the error and return undefined.
    exports.reportException("getProperty", e);
  }
  return undefined;
};

/**
 * Determines if a descriptor has a getter which doesn't call into JavaScript.
 *
 * @param Object aDesc
 *        The descriptor to check for a safe getter.
 * @return Boolean
 *         Whether a safe getter was found.
 */
exports.hasSafeGetter = function hasSafeGetter(aDesc) {
  let fn = aDesc.get;
  return fn && fn.callable && fn.class == "Function" && fn.script === undefined;
};

/**
 * Check if it is safe to read properties and execute methods from the given JS
 * object. Safety is defined as being protected from unintended code execution
 * from content scripts (or cross-compartment code).
 *
 * See bugs 945920 and 946752 for discussion.
 *
 * @type Object aObj
 *       The object to check.
 * @return Boolean
 *         True if it is safe to read properties from aObj, or false otherwise.
 */
exports.isSafeJSObject = function isSafeJSObject(aObj) {
  if (Cu.getGlobalForObject(aObj) ==
      Cu.getGlobalForObject(exports.isSafeJSObject)) {
    return true; // aObj is not a cross-compartment wrapper.
  }

  let principal = Services.scriptSecurityManager.getObjectPrincipal(aObj);
  if (Services.scriptSecurityManager.isSystemPrincipal(principal)) {
    return true; // allow chrome objects
  }

  return Cu.isXrayWrapper(aObj);
};
