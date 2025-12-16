import tkinter as tk
from tkinter import messagebox, simpledialog, filedialog
import os
import json
from tkinter import ttk
import pywifi
from pywifi import const
import platform
import subprocess
import re
import threading
import time
import pygame
import dbus
from dbus.mainloop.glib import DBusGMainLoop
import locale
import getpass

CONFIG_PATH = os.path.join(os.path.dirname(__file__), "config.json")
ASSETS_DIR = os.path.join(os.path.dirname(__file__), "assets")

# Initialize pygame mixer once
pygame.mixer.init()

def play_sound(filename, wait=False):
    def play():
        try:
            path = os.path.join(ASSETS_DIR, filename)
            if not pygame.mixer.get_init():
                pygame.mixer.init()  # Re-initialize if uninitialized
            sound = pygame.mixer.Sound(path)
            sound.play()
            if wait:
                while pygame.mixer.get_busy():
                    time.sleep(0.1)
        except Exception as e:
            print(f"Error playing sound '{filename}': {e}")

    if wait:
        play()
    else:
        threading.Thread(target=play, daemon=True).start()

def load_config():
    if os.path.exists(CONFIG_PATH):
        with open(CONFIG_PATH, "r") as f:
            return json.load(f)
    return {
        "wifi_networks": [],
        "wifi_enabled": False,
        "volume": 50,
        "background": "",
        "password": "",
        "brightness": 50,
        "input_language": "en_US.UTF-8"
    }

def save_config(config):
    with open(CONFIG_PATH, "w") as f:
        json.dump(config, f, indent=4)

def scan_wifi():
    try:
        wifi = pywifi.PyWiFi()
        if not wifi.interfaces():
            print("No Wi-Fi interfaces found.")
            return []
        iface = wifi.interfaces()[0]
        iface.scan()
        time.sleep(2)  # Wait for scan to complete
        networks = iface.scan_results()
        print(f"Scanned {len(networks)} Wi-Fi networks.")
        return networks
    except PermissionError as e:
        print(f"Permission denied: {e}. Run with 'sudo ./wifi_helper' or configure PolicyKit for automatic privileges.")
        play_sound("error.wav")
        return []
    except Exception as e:
        print(f"Error scanning Wi-Fi: {e}")
        play_sound("error.wav")
        return []

def get_battery_status():
    try:
        bus = dbus.SystemBus()
        upower = bus.get_object('org.freedesktop.UPower', '/org/freedesktop/UPower/devices/battery_BAT0')
        upower_interface = dbus.Interface(upower, 'org.freedesktop.DBus.Properties')
        percentage = upower_interface.Get('org.freedesktop.UPower.Device', 'Percentage')
        state = upower_interface.Get('org.freedesktop.UPower.Device', 'State')
        states = {1: "Charging", 2: "Discharging", 4: "Fully charged"}
        return f"{int(percentage)}% ({states.get(state, 'Unknown')})"
    except Exception as e:
        print(f"Error getting battery status: {e}")
        return "Battery status unavailable"

def get_max_brightness():
    """Get the maximum brightness value for the backlight device"""
    try:
        with open('/sys/class/backlight/intel_backlight/max_brightness') as f:
            return int(f.read().strip())
    except Exception as e:
        print(f"Error getting max brightness: {e}")
        return 100  # Fallback value

def set_brightness(percentage):
    """Set brightness as a percentage (0-100)"""
    try:
        max_brightness = get_max_brightness()
        # Convert percentage to actual brightness value
        brightness_value = int((percentage / 100) * max_brightness)
        with open('/sys/class/backlight/intel_backlight/brightness', 'w') as f:
            f.write(str(brightness_value))
    except PermissionError:
        print("Permission denied: Run with sudo for brightness control")
        play_sound("error.wav")
    except Exception as e:
        print(f"Error setting brightness: {e}")
        play_sound("error.wav")

def get_brightness():
    """Get current brightness as a percentage (0-100)"""
    try:
        max_brightness = get_max_brightness()
        with open('/sys/class/backlight/intel_backlight/brightness') as f:
            current_value = int(f.read().strip())
        # Convert actual brightness value to percentage
        percentage = int((current_value / max_brightness) * 100)
        return percentage
    except Exception as e:
        print(f"Error getting brightness: {e}")
        return 50

def get_security_logs():
    try:
        log_file = "/var/log/auth.log"
        if os.path.exists(log_file):
            with open(log_file, 'r') as f:
                logs = f.readlines()[-10:]  # Last 10 lines
            return "\n".join(logs)
        return "No security logs found."
    except PermissionError:
        print("Permission denied: Run with sudo to view logs")
        return "Permission denied"
    except Exception as e:
        print(f"Error reading security logs: {e}")
        return "Error reading logs"

def get_available_languages():
    try:
        result = subprocess.run(['locale', '-a'], capture_output=True, text=True)
        return result.stdout.strip().split('\n')
    except Exception as e:
        print(f"Error getting languages: {e}")
        return ['en_US.UTF-8']

def set_input_language(lang):
    try:
        subprocess.run(['setxkbmap', lang.split('.')[0]], check=True)
        return True
    except Exception as e:
        print(f"Error setting language: {e}")
        play_sound("error.wav")
        return False

def manage_bluetooth(action):
    try:
        bus = dbus.SystemBus()
        manager = bus.get_object('org.bluez', '/')
        adapter_path = dbus.Interface(manager, 'org.freedesktop.DBus.ObjectManager').GetManagedObjects()
        for path, interfaces in adapter_path.items():
            if 'org.bluez.Adapter1' in interfaces:
                adapter = bus.get_object('org.bluez', path)
                adapter_iface = dbus.Interface(adapter, 'org.bluez.Adapter1')
                if action == "power_on":
                    adapter_iface.Set('org.bluez.Adapter1', 'Powered', True)
                    return "Bluetooth powered on"
                elif action == "power_off":
                    adapter_iface.Set('org.bluez.Adapter1', 'Powered', False)
                    return "Bluetooth powered off"
                elif action == "scan":
                    adapter_iface.StartDiscovery()
                    time.sleep(5)
                    adapter_iface.StopDiscovery()
                    devices = []
                    objects = dbus.Interface(manager, 'org.freedesktop.DBus.ObjectManager').GetManagedObjects()
                    for path, interfaces in objects.items():
                        if 'org.bluez.Device1' in interfaces:
                            dev = bus.get_object('org.bluez', path)
                            dev_props = dbus.Interface(dev, 'org.freedesktop.DBus.Properties')
                            name = dev_props.Get('org.bluez.Device1', 'Name')
                            devices.append(name)
                    return devices if devices else ["No devices found"]
        return ["No Bluetooth adapter found"]
    except Exception as e:
        print(f"Error managing Bluetooth: {e}")
        play_sound("error.wav")
        return ["Bluetooth error"]

def open_settings(parent):
    config = load_config()

    win = tk.Toplevel(parent)
    win.title("Settings")
    win.geometry("600x300")
    win.configure(bg='#000000')

    notebook = ttk.Notebook(win)
    notebook.pack(expand=True, fill="both", padx=5, pady=5)

    # Network Tab
    network_frame = tk.Frame(notebook, bg="#000000")
    notebook.add(network_frame, text="Network")
    tk.Label(network_frame, text="Network", font=("Arial", 14), fg="#33B5E5", bg="#000000").pack(pady=5)
    def wifi_command():
        wifi_win = tk.Toplevel(win)
        wifi_win.title("Wi-Fi Networks")
        wifi_win.geometry("300x200")
        wifi_win.configure(bg="#000000")
        try:
            networks = scan_wifi()
            if not networks:
                tk.Label(wifi_win, text="No Wi-Fi networks found or scan failed. Run with 'sudo ./wifi_helper' or configure PolicyKit for automatic privileges.",
                        bg="#000000", fg="#33B5E5", font=("Arial", 10)).pack(pady=10)
            else:
                for net in networks:
                    ssid = net.ssid
                    if ssid:
                        btn_text = f"{ssid} (Signal: {net.signal} dBm)"
                        tk.Button(wifi_win, text=btn_text, bg="#222222", fg="#33B5E5",
                                command=lambda n=ssid: connect_disconnect_wifi(n, config, win)).pack(pady=2)
                tk.Button(wifi_win, text="Turn Off Wi-Fi" if config["wifi_enabled"] else "Turn On Wi-Fi",
                        bg="#222222", fg="#33B5E5", command=lambda: toggle_wifi(config)).pack(pady=5)
        except Exception as e:
            print(f"Error in wifi_command: {e}")
            tk.Label(wifi_win, text="Error loading Wi-Fi networks.",
                    bg="#000000", fg="#FF0000", font=("Arial", 10)).pack(pady=10)
            play_sound("error.wav")

    def bluetooth_command():
        bt_win = tk.Toplevel(win)
        bt_win.title("Bluetooth Settings")
        bt_win.geometry("300x200")
        bt_win.configure(bg="#000000")
        tk.Button(bt_win, text="Power On", bg="#222222", fg="#33B5E5",
                 command=lambda: messagebox.showinfo("Bluetooth", manage_bluetooth("power_on"), parent=bt_win)).pack(pady=2)
        tk.Button(bt_win, text="Power Off", bg="#222222", fg="#33B5E5",
                 command=lambda: messagebox.showinfo("Bluetooth", manage_bluetooth("power_off"), parent=bt_win)).pack(pady=2)
        tk.Button(bt_win, text="Scan Devices", bg="#222222", fg="#33B5E5",
                 command=lambda: messagebox.showinfo("Bluetooth", "\n".join(manage_bluetooth("scan")), parent=bt_win)).pack(pady=2)

    tk.Button(network_frame, text="Wi-Fi", width=20, bg="#222222", fg="#33B5E5", command=wifi_command).pack(pady=2)
    tk.Button(network_frame, text="Bluetooth", width=20, bg="#222222", fg="#33B5E5", command=bluetooth_command).pack(pady=2)

    # Device Tab
    device_frame = tk.Frame(notebook, bg="#000000")
    notebook.add(device_frame, text="Device")
    tk.Label(device_frame, text="Device", font=("Arial", 14), fg="#33B5E5", bg="#000000").pack(pady=5)
    def volume_command():
        vol_win = tk.Toplevel(win)
        vol_win.title("Adjust Volume")
        vol_win.geometry("120x140")
        vol_win.configure(bg='#2D2D30')
        vol_win.overrideredirect(True)

        control_frame = tk.Frame(vol_win, bg='#2D2D30')
        control_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        current_volume = get_system_volume()
        if current_volume == 0:
            icon = "🔇"
        elif current_volume < 30:
            icon = "🔈"
        elif current_volume < 70:
            icon = "🔉"
        else:
            icon = "🔊"
        volume_icon = tk.Label(control_frame, text=icon, font=('Arial', 16), bg='#2D2D30', fg='white')
        volume_icon.pack(pady=(0, 10))

        volume_var = tk.IntVar(value=current_volume)
        volume_slider = tk.Scale(control_frame, from_=0, to=100, orient=tk.VERTICAL, variable=volume_var,
                                command=lambda val: on_volume_change(int(val), config, volume_icon, volume_percent),
                                bg='#2D2D30', fg='white', highlightthickness=0, troughcolor='#404040',
                                activebackground='#0078D4', length=80)
        volume_slider.pack()
        volume_slider.bind("<Button-1>", lambda e: play_sound("ding.wav"))

        volume_percent = tk.Label(control_frame, text=f"{current_volume}%", bg='#2D2D30', fg='white', font=('Arial', 10))
        volume_percent.pack(pady=(5, 0))

        x = win.winfo_rootx() + 50
        y = win.winfo_rooty() - 150
        vol_win.geometry(f"+{x}+{y}")

        vol_win.after(3000, lambda: close_volume_popup(vol_win))

    def on_volume_change(value, config, icon_label, percent_label):
        set_system_volume(value)
        config["volume"] = value
        save_config(config)
        if value == 0:
            icon = "🔇"
        elif value < 30:
            icon = "🔈"
        elif value < 70:
            icon = "🔉"
        else:
            icon = "🔊"
        icon_label.config(text=icon)
        percent_label.config(text=f"{value}%")

    def close_volume_popup(window):
        if window.winfo_exists():
            window.destroy()

    def brightness_command():
        bright_win = tk.Toplevel(win)
        bright_win.title("Adjust Brightness")
        bright_win.geometry("120x140")
        bright_win.configure(bg='#2D2D30')
        bright_win.overrideredirect(True)

        control_frame = tk.Frame(bright_win, bg='#2D2D30')
        control_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        brightness_icon = tk.Label(control_frame, text="☀", font=('Arial', 16), bg='#2D2D30', fg='white')
        brightness_icon.pack(pady=(0, 10))

        current_brightness = get_brightness()
        brightness_var = tk.IntVar(value=current_brightness)
        
        brightness_percent = tk.Label(control_frame, text=f"{current_brightness}%", bg='#2D2D30', fg='white', font=('Arial', 10))
        
        brightness_slider = tk.Scale(control_frame, from_=100, to=0, orient=tk.VERTICAL, variable=brightness_var,
                                   command=lambda val: on_brightness_change(int(val), config, brightness_percent),
                                   bg='#2D2D30', fg='white', highlightthickness=0, troughcolor='#404040',
                                   activebackground='#0078D4', length=80, showvalue=0)
        brightness_slider.pack()
        brightness_slider.bind("<Button-1>", lambda e: play_sound("ding.wav"))

        brightness_percent.pack(pady=(5, 0))

        x = win.winfo_rootx() + 50
        y = win.winfo_rooty() - 150
        bright_win.geometry(f"+{x}+{y}")

        bright_win.after(3000, lambda: close_volume_popup(bright_win))

    def on_brightness_change(value, config, percent_label):
        set_brightness(value)
        config["brightness"] = value
        save_config(config)
        percent_label.config(text=f"{value}%")

    def battery_command():
        status = get_battery_status()
        messagebox.showinfo("Battery Status", status, parent=win)

    def language_command():
        lang_win = tk.Toplevel(win)
        lang_win.title("Select Input Language")
        lang_win.geometry("300x200")
        lang_win.configure(bg="#000000")
        languages = get_available_languages()
        selected_lang = tk.StringVar(value=config.get("input_language", "en_US.UTF-8"))
        for lang in languages:
            tk.Radiobutton(lang_win, text=lang, value=lang, variable=selected_lang, bg="#000000", fg="#33B5E5",
                          command=lambda: on_language_change(selected_lang.get(), config)).pack(anchor="w", padx=10)
        tk.Button(lang_win, text="Apply", bg="#222222", fg="#33B5E5",
                 command=lambda: on_language_change(selected_lang.get(), config)).pack(pady=5)

    def on_language_change(lang, config):
        if set_input_language(lang):
            config["input_language"] = lang
            save_config(config)
            messagebox.showinfo("Language", f"Input language set to {lang}", parent=win)

    tk.Button(device_frame, text="Audio Volume", width=20, bg="#222222", fg="#33B5E5", command=volume_command).pack(pady=2)
    tk.Button(device_frame, text="Display Brightness", width=20, bg="#222222", fg="#33B5E5", command=brightness_command).pack(pady=2)
    tk.Button(device_frame, text="Battery", width=20, bg="#222222", fg="#33B5E5", command=battery_command).pack(pady=2)
    tk.Button(device_frame, text="Input Language", width=20, bg="#222222", fg="#33B5E5", command=language_command).pack(pady=2)

    # Security Tab
    security_frame = tk.Frame(notebook, bg="#000000")
    notebook.add(security_frame, text="Security")
    tk.Label(security_frame, text="Security", font=("Arial", 14), fg="#33B5E5", bg="#000000").pack(pady=5)
    def set_password():
        pw = simpledialog.askstring("Set Password", "Enter new password:", show="*", parent=win)
        if pw:
            config["password"] = pw
            save_config(config)
            messagebox.showinfo("Password", "Password saved successfully for the desktop environment.", parent=win)

    def view_security_logs():
        logs = get_security_logs()
        log_win = tk.Toplevel(win)
        log_win.title("Security Logs")
        log_win.geometry("400x300")
        log_win.configure(bg="#000000")
        tk.Label(log_win, text="Recent Security Logs", font=("Arial", 12), fg="#33B5E5", bg="#000000").pack(pady=5)
        text_area = tk.Text(log_win, height=10, bg="#222222", fg="#33B5E5", font=("Arial", 10))
        text_area.insert(tk.END, logs)
        text_area.config(state='disabled')
        text_area.pack(padx=10, pady=5, fill="both", expand=True)

    tk.Button(security_frame, text="Change Password", width=20, bg="#222222", fg="#33B5E5", command=set_password).pack(pady=2)
    tk.Button(security_frame, text="Security Logs", width=20, bg="#222222", fg="#33B5E5", command=view_security_logs).pack(pady=2)

    # More Tab
    more_frame = tk.Frame(notebook, bg="#000000")
    notebook.add(more_frame, text="More")
    tk.Label(more_frame, text="More", font=("Arial", 14), fg="#33B5E5", bg="#000000").pack(pady=5)
    def set_background():
        path = filedialog.askopenfilename(filetypes=[("Image Files", "*.jpg *.png *.jpeg")], parent=win)
        if path:
            config["background"] = path
            save_config(config)
            messagebox.showinfo("Background", "Background image updated.\nRestart the desktop to apply.", parent=win)
    def reset_settings():
        if messagebox.askyesno("Reset", "Are you sure you want to reset to default settings?", parent=win):
            config.clear()
            config.update({
                "wifi_networks": [],
                "wifi_enabled": False,
                "volume": 50,
                "background": "",
                "password": "",
                "brightness": 50,
                "input_language": "en_US.UTF-8"
            })
            save_config(config)
            messagebox.showinfo("Reset", "Settings reset to default.\nRestart the desktop to apply.", parent=win)
    tk.Button(more_frame, text="Change Background", width=20, bg="#222222", fg="#33B5E5", command=set_background).pack(pady=2)
    tk.Button(more_frame, text="Reset to Default", width=20, bg="#222222", fg="#33B5E5", command=reset_settings).pack(pady=2)

    win.update()
    return win

def connect_disconnect_wifi(ssid, config, parent_win):
    wifi = pywifi.PyWiFi()
    iface = wifi.interfaces()[0]
    profile = pywifi.Profile()
    profile.ssid = ssid
    profile.auth = const.AUTH_ALG_OPEN
    profile.akm.append(const.AKM_TYPE_WPA2PSK)
    profile.cipher = const.CIPHER_TYPE_CCMP
    password = simpledialog.askstring("Password", f"Enter password for {ssid}:", show="*", parent=parent_win)
    if password:
        profile.key = password
        iface.remove_all_network_profiles()
        tmp_profile = iface.add_network_profile(profile)
        iface.connect(tmp_profile)
        time.sleep(5)
        if iface.status() == const.IFACE_CONNECTED:
            messagebox.showinfo("Wi-Fi", f"Connected to {ssid}", parent=parent_win)
        else:
            messagebox.showerror("Wi-Fi", f"Failed to connect to {ssid}", parent=parent_win)
            play_sound("error.wav")

def toggle_wifi(config):
    wifi = pywifi.PyWiFi()
    iface = wifi.interfaces()[0]
    if config["wifi_enabled"]:
        iface.disconnect()
        config["wifi_enabled"] = False
        messagebox.showinfo("Wi-Fi", "Wi-Fi turned off")
    else:
        iface.connect()
        config["wifi_enabled"] = True
        messagebox.showinfo("Wi-Fi", "Wi-Fi turned on")
    save_config(config)

def get_system_volume():
    try:
        result = subprocess.run(['amixer', 'get', 'Master'], capture_output=True, text=True)
        if result.returncode == 0:
            match = re.search(r'\[(\d+)%\]', result.stdout)
            if match:
                return int(match.group(1))
    except Exception as e:
        print(f"Error getting volume: {e}")
    return 50

def set_system_volume(volume):
    try:
        subprocess.run(['amixer', 'set', 'Master', f'{volume}%'], check=True)
    except Exception as e:
        print(f"Error setting volume: {e}")

if __name__ == "__main__":
    DBusGMainLoop(set_as_default=True)
    root = tk.Tk()
    root.withdraw()
    open_settings(root)
    root.mainloop()