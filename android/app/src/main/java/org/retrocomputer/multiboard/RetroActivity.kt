package org.retrocomputer.multiboard

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
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
    // chosen last, the MSX's 50/60Hz, and so on.
    RetroNative.nativeStart(filesDir.absolutePath)
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
      if (RetroNative.nativeKey(code, event.action == KeyEvent.ACTION_DOWN)) return true
    }
    return super.dispatchKeyEvent(event)
  }

  // A tap anywhere opens the selector, as on the boards.
  override fun onTouchEvent(event: MotionEvent): Boolean {
    if (event.actionMasked == MotionEvent.ACTION_UP && !menuOpen) RetroNative.nativeOpenMenu()
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
    val items = (0 until n).map { RetroNative.nativeEntryName(m, it) } + getString(R.string.other_computer)
    AlertDialog.Builder(this)
      .setTitle(RetroNative.nativeMachineName(m))
      .setItems(items.toTypedArray()) { _, which ->
        if (which < n) close(which) else pickMachine()
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
    if (machine == RetroNative.nativeCurrentMachine()) { close(entry); return }
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
