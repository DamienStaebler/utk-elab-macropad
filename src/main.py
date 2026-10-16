from macropad import MacroPad
import subprocess
from time import sleep
import pyautogui
from pynput.keyboard import Controller, Key

app = MacroPad(default_profile="default")

default = app.on_profile("default")

# --- Single decorator for press & release ---
@default.button(1, 1)
def media_prev(event):
    if event.is_release:
        event.keyboard.press(Key.media_prev)
        event.keyboard.release(Key.media_prev)

@default.button(1, 2)
def media_play_pause(event):
    # Perform action on release only
    if event.is_release:
        event.keyboard.press(Key.media_play_pause)
        event.keyboard.release(Key.media_play_pause)

@default.button(1, 3)
def media_next(event):
    # Perform action on release only
    if event.is_release:
        event.keyboard.press(Key.media_next)
        event.keyboard.release(Key.media_next)

@default.button(2, 1)
def study_music(event):
    if event.is_release:
        print("opening study music...")
        subprocess.Popen(["/opt/YouTube Music/youtube-music"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# rotery encoder
@default.encoder("+1")
def vol_up(event):
    event.keyboard.press(Key.media_volume_up)
    event.keyboard.release(Key.media_volume_up)

@default.encoder("-1")
def vol_down(event):
    event.keyboard.press(Key.media_volume_down)
    event.keyboard.release(Key.media_volume_down)

@default.button(1, 4)
def mute_system(event):
    if event.is_release:
        print("muting system...")
        event.keyboard.press(Key.media_volume_mute)
        event.keyboard.release(Key.media_volume_mute)

@default.button(2, 4)
def work(event):
    if event.is_release:
        event.switch_profile("work")


# work profile
work = app.on_profile("work")

@work.button(2, 4)
def default_profile(event):
    if event.is_release:
        event.switch_profile("default")

@work.button(1, 1)
def ms_teams(event):
    print("opening ms teams...")
    if event.is_release:
        subprocess.Popen(["xdg-open", "https://teams.cloud.microsoft/"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

@work.button(1, 2)
def open_mail(event):
    if event.is_release:
        subprocess.Popen(["xdg-open", "https://mail.google.com/mail/u/1/#inbox"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

@work.button(1, 3)
def close_window(event):
    if event.is_press:
        pyautogui.hotkey("ctrl", "w")

if __name__ == "__main__":
    try:
        app.run()
    except KeyboardInterrupt:
        print("\nExiting host daemon.")
