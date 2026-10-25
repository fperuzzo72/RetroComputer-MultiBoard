package org.retrocomputer.multiboard

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import java.io.File
import java.lang.ref.WeakReference

/**
 * The RetroComputer's whole Activity: one SurfaceView the machine's picture
 * is posted to, the keys, and the selector as a dialog.
 *
 * As in the Android CrossPoint, there is no layout and no view but the
 * surface: the machines draw their own screens. The emulation itself runs
 * in native threads (src/boards/android); this only hands it a surface,
 * keys and a place to keep its settings.
 */
class RetroActivity : Activity(), SurfaceHolder.Callback {

  private lateinit var surfaceView: SurfaceView
  private var menuOpen = false

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    // A computer in use does not go to sleep under you. On e-ink a still
    // screen costs almost nothing.
    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

    surfaceView = SurfaceView(this)
    surfaceView.holder.addCallback(this)
    // Not focusable for text: the keyboard's keys must reach the machine as
    // keys, not go through an input method that composes them first. The
    // machines do their own US-International.
    surfaceView.isFocusable = true
    surfaceView.isFocusableInTouchMode = true
    setContentView(surfaceView)
    surfaceView.requestFocus()
    hideSystemBars()

    RetroNative.activity = WeakReference(this)
    // NVS lives in the app's own files: which computer and which game were
    // chosen last, the MSX's 50/60Hz, and so on. The card is a folder.
    RetroNative.nativeStart(filesDir.absolutePath, cardFolder().absolutePath)
  }

  /**
   * The folder that plays the boards' SD card: pc/ for the PC's disk
   * images, c64/ for its games, mac/ for the Mac's discs.
   *
   * With All files access, `/sdcard/RetroComputer`: a folder anyone can see
   * and fill from a file manager or over USB, which survives uninstalling.
   * Without it, the app's own external folder, which Android 11+ hides.
   * Unlike the Android CrossPoint the app still starts without it: the MSX
   * and the Spectrum carry their ROMs inside and need no file at all. It
   * asks once, and a later start finds the permission there.
   */
  private fun cardFolder(): File {
    if (Environment.isExternalStorageManager()) {
      val dir = File(Environment.getExternalStorageDirectory(), "RetroComputer")
      if (dir.mkdirs() || dir.isDirectory) {
        for (sub in listOf("pc", "c64", "mac")) File(dir, sub).mkdirs()
        return dir
      }
    } else {
      val prefs = getSharedPreferences("retro", MODE_PRIVATE)
      if (!prefs.getBoolean("asked_files", false)) {
        prefs.edit().putBoolean("asked_files", true).apply()
        runCatching {
          startActivity(
            Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName"))
          )
        }
      }
    }
    val fallback = getExternalFilesDir(null) ?: filesDir
    for (sub in listOf("pc", "c64", "mac")) File(fallback, sub).mkdirs()
    return fallback
  }

  override fun onResume() {
    super.onResume()
    hideSystemBars()
  }

  @Suppress("DEPRECATION")
  private fun hideSystemBars() {
    if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
      window.insetsController?.let {
        it.hide(WindowInsets.Type.systemBars())
        it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
      }
    } else {
      window.decorView.systemUiVisibility =
        View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
          View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
    }
  }

  // --- surface -----------------------------------------------------------------

  override fun surfaceCreated(holder: SurfaceHolder) {
    RetroNative.nativeSetSurface(holder.surface)
  }

  override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
    RetroNative.nativeSetSurface(holder.surface)
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    // Synchronous: the native side takes the same lock as a post in
    // progress, so this waits for it and nothing writes to a dead surface.
    RetroNative.nativeSetSurface(null)
  }

  // --- keys ----------------------------------------------------------------------

  override fun dispatchKeyEvent(event: KeyEvent): Boolean {
    if (menuOpen) return super.dispatchKeyEvent(event)
    val code = event.keyCode
    val physical = event.device?.let { !it.isVirtual && it.sources and InputDevice.SOURCE_KEYBOARD != 0 } ?: false
    // The system's back (a gesture, or the button on the panel) opens the
    // selector rather than closing the computer. Escape on a keyboard is the
    // machine's own key.
    if (code == KeyEvent.KEYCODE_BACK && !physical) {
      if (event.action == KeyEvent.ACTION_UP) RetroNative.nativeOpenMenu()
      return true
    }
    if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount > 0) return true   /* the machine repeats */
    if (event.action == KeyEvent.ACTION_DOWN || event.action == KeyEvent.ACTION_UP) {
      val down = event.action == KeyEvent.ACTION_DOWN
      if (functionKey(code, down, event.isCtrlPressed && event.isAltPressed)) return true
      if (RetroNative.nativeKey(code, down)) return true
    }
    return super.dispatchKeyEvent(event)
  }

  // The Boox takes F3 to F11 for itself (F5 a screenshot, F8 home, the rest
  // system panels) before any app sees them; F1 and F2 get through. And it
  // swallows the Windows/Command key's combinations too. So Ctrl+Alt with
  // 1-9, 0, - and = is F1-F12, for every machine but the Mac, which never
  // had function keys. Ctrl and Alt reached the machine on their way down:
  // they are let go there first, or a PC would see Ctrl+Alt+F3.
  private val fHeld = HashMap<Int, Int>()   /* key pressed -> F key sent */

  private fun functionKey(code: Int, down: Boolean, ctrlAlt: Boolean): Boolean {
    if (RetroNative.nativeHasPointer() && RetroNative.nativeMachineName(RetroNative.nativeCurrentMachine()).startsWith("Mac")) return false
    if (!down) {
      val f = fHeld.remove(code) ?: return false
      RetroNative.nativeKey(f, false)
      return true
    }
    if (!ctrlAlt) return false
    val f = when (code) {
      in KeyEvent.KEYCODE_1..KeyEvent.KEYCODE_9 -> KeyEvent.KEYCODE_F1 + (code - KeyEvent.KEYCODE_1)
      KeyEvent.KEYCODE_0 -> KeyEvent.KEYCODE_F10
      KeyEvent.KEYCODE_MINUS -> KeyEvent.KEYCODE_F11
      KeyEvent.KEYCODE_EQUALS -> KeyEvent.KEYCODE_F12
      else -> return false
    }
    for (m in intArrayOf(KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT, KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT))
      RetroNative.nativeKey(m, false)
    fHeld[code] = f
    android.util.Log.i("RetroKeys", "Ctrl+Alt+${KeyEvent.keyCodeToString(code).removePrefix("KEYCODE_")} = F${f - KeyEvent.KEYCODE_F1 + 1}")
    RetroNative.nativeKey(f, true)
    return true
  }

  // A machine with a pointer (the Mac) takes the panel as a trackpad, as on
  // the e-ink boards; its selector is then the system's back. For the
  // others a tap anywhere opens the selector, as on the boards.
  override fun onTouchEvent(event: MotionEvent): Boolean {
    if (menuOpen) return true
    if (RetroNative.nativeHasPointer()) {
      val touching = when (event.actionMasked) {
        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> false
        else -> true
      }
      // A second finger while the first stays down is the right button,
      // held until it lifts; the first finger keeps moving the pointer.
      when (event.actionMasked) {
        MotionEvent.ACTION_POINTER_DOWN -> if (event.pointerCount == 2) RetroNative.nativeTouchSecond(true)
        MotionEvent.ACTION_POINTER_UP -> if (event.pointerCount == 2) RetroNative.nativeTouchSecond(false)
        MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> RetroNative.nativeTouchSecond(false)
      }
      RetroNative.nativeTouch(touching, event.getX(0), event.getY(0), event.eventTime)
      return true
    }
    if (event.actionMasked == MotionEvent.ACTION_UP) RetroNative.nativeOpenMenu()
    return true
  }

  // --- the selector -------------------------------------------------------------
  // The machine stands still from the moment it asked until an answer comes
  // back through nativeMenuDone (an entry of its own) or nativeChooseMachine
  // (another computer, which starts the app over as it).

  fun showMenu() {
    if (menuOpen) return
    menuOpen = true
    val m = RetroNative.nativeCurrentMachine()
    val n = RetroNative.nativeEntryCount(m)
    // Another computer first: with a card full of games it was at the end
    // of a long list.
    val items = listOf(getString(R.string.other_computer)) + (0 until n).map { RetroNative.nativeEntryName(m, it) }
    AlertDialog.Builder(this)
      .setTitle(RetroNative.nativeMachineName(m))
      .setItems(items.toTypedArray()) { _, which ->
        if (which == 0) pickMachine()
        else if (RetroNative.nativeHasPointer()) choose(m, which - 1)
        else close(which - 1)
      }
      .setNegativeButton(R.string.back) { _, _ -> close(-1) }
      .setOnCancelListener { close(-1) }
      .show()
  }

  private fun pickMachine() {
    val count = RetroNative.nativeMachineCount()
    val names = (0 until count).map { RetroNative.nativeMachineName(it) }
    AlertDialog.Builder(this)
      .setTitle(R.string.which_computer)
      .setItems(names.toTypedArray()) { _, which -> pickEntry(which) }
      .setNegativeButton(R.string.back) { _, _ -> close(-1) }
      .setOnCancelListener { close(-1) }
      .show()
  }

  private fun pickEntry(machine: Int) {
    val n = RetroNative.nativeEntryCount(machine)
    if (n <= 1) { choose(machine, 0); return }
    val items = (0 until n).map { RetroNative.nativeEntryName(machine, it) }
    AlertDialog.Builder(this)
      .setTitle(RetroNative.nativeMachineName(machine))
      .setItems(items.toTypedArray()) { _, which -> choose(machine, which) }
      .setNegativeButton(R.string.back) { _, _ -> close(-1) }
      .setOnCancelListener { close(-1) }
      .show()
  }

  private fun choose(machine: Int, entry: Int) {
    // The Mac cannot change disc in place (it never asks its selector):
    // another disc is a start over, as another computer is.
    if (machine == RetroNative.nativeCurrentMachine() && !RetroNative.nativeHasPointer()) { close(entry); return }
    menuOpen = false
    RetroNative.nativeChooseMachine(machine, entry)
  }

  private fun close(entry: Int) {
    menuOpen = false
    RetroNative.nativeMenuDone(entry)
    hideSystemBars()
  }

  /** Start over as the computer now chosen: its memory and threads are the
   *  old one's, so the process goes and a fresh one comes up. */
  fun restart() {
    val intent = Intent(this, RetroActivity::class.java)
      .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
    startActivity(intent)
    Runtime.getRuntime().exit(0)
  }
}
