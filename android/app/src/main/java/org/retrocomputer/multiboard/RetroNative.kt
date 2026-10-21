package org.retrocomputer.multiboard

import java.lang.ref.WeakReference

/**
 * The bridge to the native board (src/boards/android). Calls go down as
 * the externals; two come back up from native threads, [onMenu] and
 * [onRestart], and are handed to the Activity on its own thread.
 */
object RetroNative {
  init {
    System.loadLibrary("retro")
  }

  var activity: WeakReference<RetroActivity>? = null

  @JvmStatic external fun nativeStart(storageDir: String)
  @JvmStatic external fun nativeSetSurface(surface: Any?)
  @JvmStatic external fun nativeKey(keycode: Int, down: Boolean): Boolean
  @JvmStatic external fun nativeOpenMenu()
  @JvmStatic external fun nativeMenuDone(entry: Int)
  @JvmStatic external fun nativeChooseMachine(machine: Int, entry: Int)
  @JvmStatic external fun nativeMachineCount(): Int
  @JvmStatic external fun nativeCurrentMachine(): Int
  @JvmStatic external fun nativeMachineName(machine: Int): String
  @JvmStatic external fun nativeEntryCount(machine: Int): Int
  @JvmStatic external fun nativeEntryName(machine: Int, entry: Int): String
  @JvmStatic external fun nativeSelectedEntry(): Int

  /** The machine asked for its selector (F12, or a tap). */
  @JvmStatic
  fun onMenu() {
    activity?.get()?.let { a -> a.runOnUiThread { a.showMenu() } }
  }

  /** Another computer was chosen: start over as it. */
  @JvmStatic
  fun onRestart() {
    activity?.get()?.let { a -> a.runOnUiThread { a.restart() } }
  }
}
