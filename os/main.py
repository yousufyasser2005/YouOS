import customtkinter as ctk
from tkinter import Menu, messagebox, Canvas
import tkinter as tk
from tkinter import ttk
import os
import sys
from PIL import Image, ImageTk, ImageDraw, ImageFilter
import subprocess
import threading
import time
import json
import hashlib
from functools import partial
import platform
import re
import math
import random
# Initialize CustomTkinter
ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("dark-blue")
# Industry Grade Color Palette
COLOR_BG_PRIMARY = "#09090b" # Zinc 950
COLOR_BG_SECONDARY = "#18181b" # Zinc 900
COLOR_BG_TERTIARY = "#27272a" # Zinc 800
COLOR_ACCENT_PRIMARY = "#3b82f6" # Blue 500
COLOR_ACCENT_HOVER = "#2563eb" # Blue 600
COLOR_TEXT_PRIMARY = "#fafafa" # Zinc 50
COLOR_TEXT_SECONDARY = "#a1a1aa" # Zinc 400
COLOR_BORDER = "#3f3f46" # Zinc 700
COLOR_SUCCESS = "#10b981" # Emerald 500
COLOR_ERROR = "#ef4444" # Red 500
COLOR_WARNING = "#f59e0b" # Amber 500
# Sound System
ASSETS_DIR = os.path.join(os.path.dirname(__file__), "assets")
SOUND_ENABLED = False
try:
    import pygame
    pygame.mixer.init()
    SOUND_ENABLED = True
except ImportError:
    print("Pygame not found. Sound disabled.")
def play_sound(filename, wait=False):
    if not SOUND_ENABLED: return
    def play():
        try:
            path = os.path.join(ASSETS_DIR, filename)
            if not os.path.exists(path):
                # Try to play a default beep if file missing, or just ignore
                return
            if not pygame.mixer.get_init():
                pygame.mixer.init()
            sound = pygame.mixer.Sound(path)
            sound.play()
            if wait:
                while pygame.mixer.get_busy():
                    time.sleep(0.1)
        except Exception as e:
            print(f"Error playing sound: {e}")
    if wait:
        play()
    else:
        threading.Thread(target=play, daemon=True).start()
# Battery Status
try:
    import psutil
    BATTERY_ENABLED = True
except ImportError:
    BATTERY_ENABLED = False
# Placeholder for external apps
def create_mock_app(name):
    def mock_func(root, *args, **kwargs):
        win = ctk.CTkToplevel(root)
        win.title(name)
        win.geometry("600x400")
        ctk.CTkLabel(win, text=f"{name}\n(Demo Mode)", font=("Inter", 24)).pack(expand=True)
        return win
    return mock_func
# Try imports
PROGRAM_MODULES = {
    "Text Editor": "text_editor",
    "Settings": "settings",
    "File Manager": "file_manager",
    "Media Viewer": "media_viewer",
    "Yousuf Browser": "yousuf_browser",
    "My Computer": "my_computer",
    "App Store": "app_store",
    "Task Manager": "task_manager",
    "Terminal": "terminal",
    "Calculator": "calculator",
    "Chess": "games.chess_game",
    "X-O": "games.xo_game",
    "Pong": "games.pong_game",
    "Recycle Bin": "recycle_bin"
}
PROGRAMS = {}
for name, module_name in PROGRAM_MODULES.items():
    try:
        if module_name == "app_store": from app_store import open_app_store as func
        elif module_name == "text_editor": from text_editor import open_text_editor as func
        elif module_name == "settings": from settings import open_settings as func
        elif module_name == "file_manager": from file_manager import open_file_explorer as func
        elif module_name == "media_viewer": from media_viewer import launch_media_viewer as func
        elif module_name == "yousuf_browser": from yousuf_browser import open_yousuf_browser as func
        elif module_name == "my_computer": from my_computer import open_my_computer as func
        elif module_name == "task_manager": from task_manager import open_task_manager as func
        elif module_name == "terminal": from terminal import open_terminal as func
        elif module_name == "calculator": from calculator import open_calculator as func
        elif module_name == "games.chess_game": from games.chess_game import open_chess_game as func
        elif module_name == "games.xo_game": from games.xo_game import open_tic_tac_toe as func
        elif module_name == "games.pong_game": from games.pong_game import open_pong_game as func
        elif module_name == "recycle_bin": from recycle_bin import open_recycle_bin as func
        else: func = create_mock_app(name)
        PROGRAMS[name] = func
    except ImportError:
        PROGRAMS[name] = create_mock_app(name)
ICONS = {
    "Text Editor": "📝", "Settings": "⚙️", "File Manager": "📁",
    "Media Viewer": "🎬", "Yousuf Browser": "🌐", "My Computer": "💻",
    "App Store": "🛍️", "Task Manager": "📊", "Terminal": "💻",
    "Calculator": "🧮", "Recycle Bin": "🗑️",
    
}
USERS_FILE = "users.json"
class DesktopApp:
    def __init__(self, root):
        self.root = root
        self.root.title("YouOS")
        self.root.attributes('-fullscreen', True)
        self.root.configure(bg=COLOR_BG_PRIMARY)
        self.users = self.load_users()
        self.current_user = None
        self.open_windows = {}
        self.icon_positions = {}
        self.drag_feedback = None
        self._warned_25 = False
        self._warned_10 = False
        self.start_menu_open = False
        self.start_menu_frame = None
        self.clock_update_id = None
        self.battery_update_id = None
        self.shutdown_animation_id = None
        self.login_animation_active = False
        self.login_after_id = None
        self.login_clock_update_id = None
        self.last_click_time = 0
        self.click_delay = 300
        self.show_boot_screen()
        self.root.bind("<Escape>", lambda e: self.toggle_start_menu(close_only=True))
    def load_users(self):
        if os.path.exists(USERS_FILE):
            try:
                with open(USERS_FILE, 'r') as f: return json.load(f)
            except: pass
        return {}
    def save_users(self):
        with open(USERS_FILE, 'w') as f: json.dump(self.users, f, indent=4)
    def hash_password(self, password):
        return hashlib.sha256(password.encode()).hexdigest()
    def save_icon_positions(self):
        if self.current_user:
            self.users[self.current_user]["icon_positions"] = self.icon_positions
            self.save_users()
    # ==========================================
    # BOOT SCREEN
    # ==========================================
    def show_boot_screen(self):
        for widget in self.root.winfo_children(): widget.destroy()
        self.boot_frame = ctk.CTkFrame(self.root, fg_color=COLOR_BG_PRIMARY)
        self.boot_frame.pack(fill=tk.BOTH, expand=True)
        self.center_container = ctk.CTkFrame(self.boot_frame, fg_color="transparent")
        self.center_container.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        self.logo_text = "YouOS"
        self.logo_label = ctk.CTkLabel(self.center_container, text="", font=("Inter", 64, "bold"), text_color=COLOR_TEXT_PRIMARY)
        self.logo_label.pack(pady=(0, 20))
        self.progress_bar = ctk.CTkProgressBar(self.center_container, width=300, height=6, corner_radius=3, progress_color=COLOR_ACCENT_PRIMARY, fg_color=COLOR_BG_TERTIARY)
        self.progress_bar.set(0)
        self.progress_bar.pack()
        self.status_label = ctk.CTkLabel(self.center_container, text="Initializing...", font=("Inter", 12), text_color=COLOR_TEXT_SECONDARY)
        self.status_label.pack(pady=(10, 0))
        play_sound("startup.wav") # Attempt to play startup sound if file exists
        self.animate_logo_text(0)
    def animate_logo_text(self, index):
        if index <= len(self.logo_text):
            self.logo_label.configure(text=self.logo_text[:index])
            self.root.after(150, lambda: self.animate_logo_text(index + 1))
        else:
            self.animate_loading(0)
    def animate_loading(self, progress):
        if progress <= 100:
            val = progress / 100
            self.progress_bar.set(val)
            if progress < 30: self.status_label.configure(text="Loading Kernel...")
            elif progress < 60: self.status_label.configure(text="Starting Services...")
            elif progress < 90: self.status_label.configure(text="Loading User Interface...")
            else: self.status_label.configure(text="Ready.")
            self.root.after(random.randint(10, 50), lambda: self.animate_loading(progress + 1))
        else:
            self.root.after(500, self.show_login_screen)
    # ==========================================
    # LOGIN SCREEN
    # ==========================================
    def show_login_screen(self):
        for widget in self.root.winfo_children(): widget.destroy()
        self.login_bg = ctk.CTkFrame(self.root, fg_color=COLOR_BG_PRIMARY)
        self.login_bg.pack(fill=tk.BOTH, expand=True)
        
        canvas = Canvas(
            self.login_bg, width=self.root.winfo_screenwidth(),
            height=self.root.winfo_screenheight(), bg=COLOR_BG_PRIMARY,
            highlightthickness=0
        )
        canvas.pack(fill="both", expand=True)
        
        # Load random wallpaper as background
        wallpapers_dir = os.path.join(os.path.dirname(__file__), "wallpapers")
        if os.path.exists(wallpapers_dir):
            wallpaper_files = [f for f in os.listdir(wallpapers_dir) 
                             if f.lower().endswith(('.png', '.jpg', '.jpeg'))]
            if wallpaper_files:
                random_wallpaper = random.choice(wallpaper_files)
                wallpaper_path = os.path.join(wallpapers_dir, random_wallpaper)
                try:
                    img = Image.open(wallpaper_path)
                    width = self.root.winfo_screenwidth()
                    height = self.root.winfo_screenheight()
                    resized = img.resize((width, height), Image.LANCZOS)
                    # Apply slight blur for aesthetic effect
                    blurred = resized.filter(ImageFilter.GaussianBlur(radius=3))
                    self.login_wallpaper = ImageTk.PhotoImage(blurred)
                    canvas.create_image(0, 0, image=self.login_wallpaper, anchor=tk.NW)
                except Exception as e:
                    print(f"Error loading wallpaper: {e}")
                    # Fallback to gradient
                    self._create_gradient_background(canvas, width, height)
            else:
                # No wallpapers found, use gradient
                width = self.root.winfo_screenwidth()
                height = self.root.winfo_screenheight()
                self._create_gradient_background(canvas, width, height)
        else:
            # Wallpapers directory doesn't exist, use gradient
            width = self.root.winfo_screenwidth()
            height = self.root.winfo_screenheight()
            self._create_gradient_background(canvas, width, height)
        
        self.login_card = ctk.CTkFrame(self.login_bg, fg_color=COLOR_BG_SECONDARY, 
                                       width=400, height=500, corner_radius=20, 
                                       border_width=1, border_color=COLOR_BORDER)
        self.login_card.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        self.login_card.pack_propagate(False)
        
        ctk.CTkLabel(self.login_card, text="YouOS", font=("Inter", 32, "bold"), 
                    text_color=COLOR_TEXT_PRIMARY).pack(pady=(50, 10))
        ctk.CTkLabel(self.login_card, text="Welcome back", font=("Inter", 14), 
                    text_color=COLOR_TEXT_SECONDARY).pack(pady=(0, 40))
        
        usernames = list(self.users.keys())
        self.username_var = tk.StringVar(value=usernames[0] if usernames else "")
        if usernames:
            self.user_menu = ctk.CTkOptionMenu(self.login_card, variable=self.username_var, 
                                              values=usernames, width=280, height=40, 
                                              fg_color=COLOR_BG_TERTIARY, 
                                              button_color=COLOR_BG_TERTIARY, 
                                              button_hover_color=COLOR_BORDER, 
                                              text_color=COLOR_TEXT_PRIMARY, 
                                              dropdown_fg_color=COLOR_BG_SECONDARY, 
                                              font=("Inter", 14))
            self.user_menu.pack(pady=(0, 15))
        
        self.password_entry = ctk.CTkEntry(self.login_card, width=280, height=40, 
                                          placeholder_text="Password", show="•", 
                                          fg_color=COLOR_BG_TERTIARY, 
                                          border_color=COLOR_BORDER, 
                                          text_color=COLOR_TEXT_PRIMARY, 
                                          font=("Inter", 14))
        self.password_entry.pack(pady=(0, 20))
        self.password_entry.bind("<Return>", lambda e: self.attempt_login())
        
        self.login_btn = ctk.CTkButton(self.login_card, text="Sign In", width=280, 
                                       height=40, fg_color=COLOR_ACCENT_PRIMARY, 
                                       hover_color=COLOR_ACCENT_HOVER, 
                                       font=("Inter", 14, "bold"), 
                                       command=self.attempt_login)
        self.login_btn.pack(pady=(0, 20))
        
        ctk.CTkButton(self.login_card, text="Create Account", fg_color="transparent", 
                     text_color=COLOR_ACCENT_PRIMARY, hover_color=COLOR_BG_TERTIARY, 
                     width=150, command=self.show_create_account).pack()
        
        # Clock and Date in bottom right (canvas-based for transparency)
        # Create labels directly on canvas for true transparency
        canvas.create_text(
            self.root.winfo_screenwidth() - 30, 
            self.root.winfo_screenheight() - 90,
            text="12:00 AM",
            font=("Inter", 36, "bold"),
            fill=COLOR_TEXT_PRIMARY,
            anchor=tk.SE,
            tags="login_time"
        )
        
        canvas.create_text(
            self.root.winfo_screenwidth() - 30,
            self.root.winfo_screenheight() - 40,
            text="01.01.2024",
            font=("Inter", 20),
            fill=COLOR_TEXT_SECONDARY,
            anchor=tk.SE,
            tags="login_date"
        )
        
        # Store canvas reference for clock updates
        self.login_canvas = canvas
        
        # Start clock update
        self.update_login_clock()
        
        # Shutdown button
        ctk.CTkButton(self.login_bg, text="⏻", width=40, height=40, 
                     fg_color=COLOR_BG_TERTIARY, hover_color=COLOR_ERROR, 
                     font=("Inter", 18), 
                     command=self.show_shutdown_screen).place(relx=0.02, rely=0.96, anchor=tk.SW)
        
        self.login_animation_active = True
        self.login_after_id = self.root.after(800, self.pulse_login)
    
    def _create_gradient_background(self, canvas, width, height):
        """Create a subtle gradient background"""
        for i in range(height):
            ratio = i / height
            r = int(15 + (20 - 15) * ratio)
            g = int(15 + (20 - 15) * ratio)
            b = int(15 + (25 - 15) * ratio)
            color = f'#{r:02x}{g:02x}{b:02x}'
            canvas.create_line(0, i, width, i, fill=color)
    
    def update_login_clock(self):
        """Update the login screen clock and date on canvas"""
        if not hasattr(self, 'login_canvas') or not self.login_canvas.winfo_exists():
            return
        try:
            current_time = time.strftime("%I:%M %p")
            current_date = time.strftime("%d.%m.%Y")
            
            # Update canvas text
            self.login_canvas.itemconfig("login_time", text=current_time)
            self.login_canvas.itemconfig("login_date", text=current_date)
        except Exception as e:
            print(f"Error updating login clock: {e}")
        
        # Schedule next update
        self.login_clock_update_id = self.root.after(1000, self.update_login_clock)
    
    def pulse_login(self):
        if self.login_animation_active and self.login_btn.winfo_exists():
            current_fg = self.login_btn.cget('fg_color')
            if current_fg == COLOR_ACCENT_PRIMARY:
                self.login_btn.configure(fg_color='#5a9cff')
            else:
                self.login_btn.configure(fg_color=COLOR_ACCENT_PRIMARY)
            self.login_after_id = self.root.after(1000, self.pulse_login)
        elif self.login_btn.winfo_exists():
            self.login_btn.configure(fg_color=COLOR_ACCENT_PRIMARY)
    def show_create_account(self):
        if self.login_after_id is not None:
            self.root.after_cancel(self.login_after_id)
        self.login_animation_active = False
        top = ctk.CTkToplevel(self.root)
        top.title("Create Account")
        top.geometry("400x500")
        top.transient(self.root)
        frame = ctk.CTkFrame(top, fg_color=COLOR_BG_PRIMARY)
        frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        ctk.CTkLabel(frame, text="New User", font=("Inter", 24, "bold")).pack(pady=20)
        user_entry = ctk.CTkEntry(frame, placeholder_text="Username")
        user_entry.pack(pady=10, fill=tk.X)
        pass_entry = ctk.CTkEntry(frame, placeholder_text="Password", show="•")
        pass_entry.pack(pady=10, fill=tk.X)
        def create():
            u, p = user_entry.get(), pass_entry.get()
            if u and p:
                self.users[u] = {"password": self.hash_password(p), "theme": "dark", "wallpaper": None, "icon_positions": {}}
                self.save_users()
                top.destroy()
                self.show_login_screen()
        ctk.CTkButton(frame, text="Create", command=create).pack(pady=20, fill=tk.X)
    def attempt_login(self):
        if self.login_after_id is not None:
            self.root.after_cancel(self.login_after_id)
        if self.login_clock_update_id is not None:
            self.root.after_cancel(self.login_clock_update_id)
        self.login_animation_active = False
        username = self.username_var.get()
        password = self.password_entry.get()
        if username in self.users and self.users[username]["password"] == self.hash_password(password):
            self.current_user = username
            self.icon_positions = self.users[username].get("icon_positions", {})
            theme = self.users[username].get("theme", "dark")
            ctk.set_appearance_mode(theme)
            play_sound("logon.wav")
            self.create_desktop()
        else:
            play_sound("error.wav")
            messagebox.showerror("Error", "Invalid credentials")
            self.password_entry.delete(0, tk.END)
    # ==========================================
    # DESKTOP
    # ==========================================
    def create_desktop(self):
        for widget in self.root.winfo_children(): widget.destroy()
        self.desktop_frame = ctk.CTkFrame(self.root, fg_color=COLOR_BG_PRIMARY)
        self.desktop_frame.pack(fill=tk.BOTH, expand=True)
        self.canvas_bg = Canvas(self.desktop_frame, bg=COLOR_BG_PRIMARY, highlightthickness=0, highlightbackground=COLOR_BG_PRIMARY)
        self.canvas_bg.pack(fill=tk.BOTH, expand=True)
        self.canvas_bg.bind("<Button-1>", lambda e: self.toggle_start_menu(close_only=True))
        self.canvas_bg.bind("<Button-3>", self.show_desktop_context_menu)
        self.update_desktop_background()
        self.drag_feedback = self.canvas_bg.create_rectangle(0, 0, 0, 0, outline=COLOR_ACCENT_PRIMARY, dash=(4, 4), width=2)
        self.canvas_bg.lower(self.drag_feedback)
        self.populate_desktop_icons()
        self.create_taskbar()
    def update_desktop_background(self):
        wallpaper = self.users[self.current_user].get("wallpaper", None)
        wallpapers_dir = os.path.join(os.path.dirname(__file__), "wallpapers")
        if wallpaper and os.path.exists(full_path := os.path.join(wallpapers_dir, wallpaper)):
            img = Image.open(full_path)
            width, height = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
            resized = img.resize((width, height), Image.LANCZOS)
            self.photo = ImageTk.PhotoImage(resized)
            self.canvas_bg.delete("bg_image")
            self.canvas_bg.create_image(0, 0, image=self.photo, anchor=tk.NW, tags="bg_image")
            self.canvas_bg.lower("bg_image")
        else:
            self.canvas_bg.delete("bg_image")
            self.canvas_bg.configure(bg=COLOR_BG_PRIMARY)
    def show_desktop_context_menu(self, event):
        try:
            menu = Menu(self.root, tearoff=0, bg=COLOR_BG_SECONDARY, fg=COLOR_TEXT_PRIMARY)
            menu.add_command(label="Personalization", command=self.open_personalization)
            menu.add_command(label="Settings", command=lambda: self.launch_program("Settings"))
            menu.post(event.x_root, event.y_root)
        except Exception as e:
            print(f"Error showing context menu: {e}")
    def open_personalization(self):
        pers_win = ctk.CTkToplevel(self.root)
        pers_win.title("Personalization")
        pers_win.geometry("500x400")
        theme_frame = ctk.CTkFrame(pers_win)
        theme_frame.pack(fill=tk.X, pady=10)
        ctk.CTkLabel(theme_frame, text="Color Theme").pack()
        for th in ["Dark", "Light"]:
            btn = ctk.CTkButton(theme_frame, text=th, command=partial(self.set_theme, th))
            btn.pack(side=tk.LEFT)
        wallpaper_frame = ctk.CTkFrame(pers_win)
        wallpaper_frame.pack(fill=tk.X, pady=10)
        ctk.CTkLabel(wallpaper_frame, text="Wallpapers").pack()
        wallpapers_dir = os.path.join(os.path.dirname(__file__), "wallpapers")
        if os.path.exists(wallpapers_dir):
            for file in os.listdir(wallpapers_dir):
                if file.lower().endswith(('.png', '.jpg', '.jpeg')):
                    btn = ctk.CTkButton(wallpaper_frame, text=file, command=partial(self.set_wallpaper, file))
                    btn.pack()
        else:
            ctk.CTkLabel(wallpaper_frame, text="No wallpapers found").pack()
    def set_theme(self, theme):
        ctk.set_appearance_mode(theme.lower())
        self.users[self.current_user]["theme"] = theme.lower()
        self.save_users()
        messagebox.showinfo("Theme", "Theme set to " + theme + ". Restart to apply fully.")
    def set_wallpaper(self, file):
        self.users[self.current_user]["wallpaper"] = file
        self.save_users()
        self.update_desktop_background()
    def populate_desktop_icons(self):
        row, col = 0, 0
        for app_name in PROGRAMS:
            default_x = 20 + (col * 100)
            default_y = 20 + (row * 100)
            x, y = self.icon_positions.get(app_name, [default_x, default_y])
            
            # Create canvas-based icon for complete transparency
            icon_id = self.canvas_bg.create_text(
                x + 40, y + 30,
                text=ICONS.get(app_name, "📦"),
                font=("Segoe UI Emoji", 32),
                fill=COLOR_TEXT_PRIMARY,
                tags=f"icon_{app_name}"
            )
            
            label_id = self.canvas_bg.create_text(
                x + 40, y + 70,
                text=app_name,
                font=("Inter", 11),
                fill=COLOR_TEXT_PRIMARY,
                width=70,
                tags=f"label_{app_name}"
            )
            
            # Store positions for dragging
            self.canvas_bg.tag_bind(f"icon_{app_name}", "<Button-1>", lambda e, n=app_name: self.on_icon_click(e, n))
            self.canvas_bg.tag_bind(f"label_{app_name}", "<Button-1>", lambda e, n=app_name: self.on_icon_click(e, n))
            self.canvas_bg.tag_bind(f"icon_{app_name}", "<Button-3>", lambda e, n=app_name: self.start_icon_drag(e, n))
            self.canvas_bg.tag_bind(f"label_{app_name}", "<Button-3>", lambda e, n=app_name: self.start_icon_drag(e, n))
            self.canvas_bg.tag_bind(f"icon_{app_name}", "<B3-Motion>", lambda e, n=app_name: self.drag_icon(e, n))
            self.canvas_bg.tag_bind(f"label_{app_name}", "<B3-Motion>", lambda e, n=app_name: self.drag_icon(e, n))
            self.canvas_bg.tag_bind(f"icon_{app_name}", "<ButtonRelease-3>", lambda e: self.end_icon_drag(e))
            self.canvas_bg.tag_bind(f"label_{app_name}", "<ButtonRelease-3>", lambda e: self.end_icon_drag(e))
            
            row += 1
            if row > 6: row, col = 0, col + 1
    
    def on_icon_click(self, event, app_name):
        current_time = time.time() * 1000
        if current_time - self.last_click_time < self.click_delay:
            self.launch_program(app_name)
        self.last_click_time = current_time
    
    def start_icon_drag(self, event, app_name):
        self.dragging_icon = app_name
        self.drag_start_x = event.x
        self.drag_start_y = event.y
        
        # Get current position
        icon_coords = self.canvas_bg.coords(f"icon_{app_name}")
        label_coords = self.canvas_bg.coords(f"label_{app_name}")
        if icon_coords:
            self.drag_icon_x = icon_coords[0]
            self.drag_icon_y = icon_coords[1]
            # Show drag feedback
            x1, y1 = self.drag_icon_x - 40, self.drag_icon_y - 30
            x2, y2 = x1 + 80, y1 + 80
            self.canvas_bg.coords(self.drag_feedback, x1, y1, x2, y2)
            self.canvas_bg.lift(self.drag_feedback)
    
    def drag_icon(self, event, app_name):
        if hasattr(self, 'dragging_icon') and self.dragging_icon == app_name:
            dx = event.x - self.drag_start_x
            dy = event.y - self.drag_start_y
            
            new_x = self.drag_icon_x + dx
            new_y = self.drag_icon_y + dy
            
            # Move icon and label
            self.canvas_bg.coords(f"icon_{app_name}", new_x, new_y)
            self.canvas_bg.coords(f"label_{app_name}", new_x, new_y + 40)
            
            # Update drag feedback
            x1, y1 = new_x - 40, new_y - 30
            x2, y2 = x1 + 80, y1 + 80
            self.canvas_bg.coords(self.drag_feedback, x1, y1, x2, y2)
            
            # Save position
            self.icon_positions[app_name] = [new_x - 40, new_y - 30]
            self.save_icon_positions()
    
    def end_icon_drag(self, event):
        self.dragging_icon = None
        self.canvas_bg.coords(self.drag_feedback, 0, 0, 0, 0)
    def create_taskbar(self):
        # Taskbar Container (Dock Style)
        self.taskbar_frame = ctk.CTkFrame(self.desktop_frame, fg_color=COLOR_BG_SECONDARY, height=60, corner_radius=20, border_width=1, border_color=COLOR_BORDER)
        self.taskbar_frame.place(relx=0.5, rely=0.96, anchor=tk.CENTER)
        # Start Button
        self.start_btn = ctk.CTkButton(self.taskbar_frame, text="You", font=("Inter", 14, "bold"), width=50, height=40, fg_color=COLOR_ACCENT_PRIMARY, hover_color=COLOR_ACCENT_HOVER, corner_radius=10, command=self.toggle_start_menu)
        self.start_btn.pack(side=tk.LEFT, padx=10, pady=10)
        ctk.CTkFrame(self.taskbar_frame, width=2, height=30, fg_color=COLOR_BORDER).pack(side=tk.LEFT, padx=5)
        # Pinned/Running Apps Area
        self.running_apps_frame = ctk.CTkFrame(self.taskbar_frame, fg_color="transparent")
        self.running_apps_frame.pack(side=tk.LEFT, padx=5)
        pinned = ["File Manager", "Yousuf Browser", "Settings", "Terminal"]
        for app in pinned:
            if app in PROGRAMS: self.create_taskbar_icon(app)
        # System Tray (Right)
        self.tray_frame = ctk.CTkFrame(self.taskbar_frame, fg_color="transparent")
        self.tray_frame.pack(side=tk.RIGHT, padx=15)
        # Battery
        self.battery_label = ctk.CTkLabel(self.tray_frame, text="🔋 --%", font=("Inter", 12), text_color=COLOR_TEXT_SECONDARY)
        self.battery_label.pack(side=tk.LEFT, padx=8)
        self.update_battery()
        # Clock and Date Container
        self.clock_date_frame = ctk.CTkFrame(self.tray_frame, fg_color="transparent")
        self.clock_date_frame.pack(side=tk.LEFT, padx=8)
        # Clock
        self.clock_label = ctk.CTkLabel(self.clock_date_frame, text="12:00 PM", font=("Inter", 12, "bold"), text_color=COLOR_TEXT_PRIMARY)
        self.clock_label.pack()
        # Date
        self.taskbar_date_label = ctk.CTkLabel(self.clock_date_frame, text="01.01.2024", font=("Inter", 9), text_color=COLOR_TEXT_SECONDARY)
        self.taskbar_date_label.pack()
        self.update_clock()
    def create_taskbar_icon(self, name):
        icon_char = ICONS.get(name, "⚡")
        btn = ctk.CTkButton(self.running_apps_frame, text=icon_char, font=("Segoe UI Emoji", 20), width=40, height=40, fg_color="transparent", hover_color=COLOR_BG_TERTIARY, corner_radius=10, command=lambda: self.launch_program(name))
        btn.pack(side=tk.LEFT, padx=4, pady=10)
    def toggle_start_menu(self, close_only=False):
        if self.start_menu_open or close_only:
            if self.start_menu_frame and self.start_menu_frame.winfo_exists():
                self.start_menu_frame.destroy()
            self.start_menu_open = False
        else:
            self.open_start_menu()
    def open_start_menu(self):
        self.start_menu_open = True
        self.start_menu_frame = ctk.CTkFrame(self.desktop_frame, width=480, height=600, fg_color=COLOR_BG_SECONDARY, corner_radius=16, border_width=1, border_color=COLOR_BORDER)
        # Position above start button
        x = self.start_btn.winfo_rootx()
        y = self.start_btn.winfo_rooty() - 600 - 8
        if y < 0:
            y = self.start_btn.winfo_rooty() + 40 + 8
        if x + 480 > self.root.winfo_screenwidth():
            x = self.root.winfo_screenwidth() - 480 - 10
        if x < 0:
            x = 10
        self.start_menu_frame.place(x=x, y=y)
        self.start_menu_frame.pack_propagate(False)
        # Header
        header_frame = ctk.CTkFrame(self.start_menu_frame, fg_color=COLOR_BG_TERTIARY, height=70, corner_radius=12)
        header_frame.pack(fill=tk.X, padx=8, pady=8)
        header_frame.pack_propagate(False)
        ctk.CTkLabel(header_frame, text="YouOS", text_color=COLOR_ACCENT_PRIMARY, font=('Inter', 18, 'bold')).pack(side=tk.LEFT, padx=16, pady=16)
        ctk.CTkLabel(header_frame, text=f"Welcome, {self.current_user}!", font=('Inter', 11), text_color=COLOR_TEXT_SECONDARY).pack(side=tk.RIGHT, padx=16, pady=16)
        # Menu content
        menu_frame = ctk.CTkFrame(self.start_menu_frame, fg_color=COLOR_BG_SECONDARY)
        menu_frame.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)
        left_panel = ctk.CTkScrollableFrame(menu_frame, fg_color=COLOR_BG_SECONDARY, width=170)
        left_panel.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 8))
        self.right_panel = ctk.CTkScrollableFrame(menu_frame, fg_color=COLOR_BG_SECONDARY, width=280)
        self.right_panel.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        def create_menu_button(parent, text, command=None):
            btn = ctk.CTkButton(parent, text=text, anchor="w", fg_color=COLOR_BG_TERTIARY, hover_color=COLOR_BORDER, font=("Inter", 11), command=command, corner_radius=6, text_color=COLOR_TEXT_PRIMARY)
            btn.pack(fill=tk.X, padx=4, pady=2)
        # Left panel buttons
        create_menu_button(left_panel, "All Programs", self.show_all_programs)
        create_menu_button(left_panel, "Terminal", lambda: [self.launch_program("Terminal"), self.toggle_start_menu(close_only=True)])
        create_menu_button(left_panel, "File Manager", lambda: [self.launch_program("File Manager"), self.toggle_start_menu(close_only=True)])
        create_menu_button(left_panel, "Settings", lambda: [self.launch_program("Settings"), self.toggle_start_menu(close_only=True)])
        create_menu_button(left_panel, "Task Manager", lambda: [self.launch_program("Task Manager"), self.toggle_start_menu(close_only=True)])
        create_menu_button(left_panel, "About", self.show_about)
        # Bottom power menu in left panel
        bottom_frame = ctk.CTkFrame(left_panel, fg_color=COLOR_BG_SECONDARY)
        bottom_frame.pack(fill=tk.X, side=tk.BOTTOM, pady=(12, 0))
        power_buttons = [
            ("🔓 Logout", self.logout),
            ("🔄 Restart", self.restart_program),
            ("⏻ Shutdown", self.shutdown_program)
        ]
        for text, command in power_buttons:
            create_menu_button(bottom_frame, text, command)
        # Frequent programs in right panel
        ctk.CTkLabel(self.right_panel, text="Frequent", text_color=COLOR_TEXT_PRIMARY, font=('Inter', 12, 'bold')).pack(anchor=tk.W, padx=8, pady=(8, 6))
        frequent_programs = ["Text Editor", "Yousuf Browser", "Media Viewer", "Calculator", "Terminal"]
        for prog in frequent_programs:
            btn = ctk.CTkButton(self.right_panel, text=prog, anchor="w", fg_color=COLOR_BG_TERTIARY, hover_color=COLOR_BORDER, font=('Inter', 11), corner_radius=6, text_color=COLOR_TEXT_PRIMARY, command=lambda p=prog: [self.launch_program(p), self.toggle_start_menu(close_only=True)])
            btn.pack(fill=tk.X, padx=6, pady=2)
        # Search section at bottom
        search_frame = ctk.CTkFrame(self.start_menu_frame, fg_color=COLOR_BG_SECONDARY, height=50)
        search_frame.pack(fill=tk.X, side=tk.BOTTOM, padx=8, pady=8)
        self.search_var = tk.StringVar()
        search_entry = ctk.CTkEntry(search_frame, fg_color=COLOR_BG_TERTIARY, text_color=COLOR_TEXT_PRIMARY, font=('Inter', 11), textvariable=self.search_var, corner_radius=8, border_color=COLOR_BORDER, border_width=1, placeholder_text="Search...")
        search_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=0, pady=0)
        self.search_var.trace_add("write", self.update_search_results)
        self.search_results_frame = ctk.CTkFrame(self.right_panel, fg_color=COLOR_BG_SECONDARY)
        self.search_results_frame.pack_forget()
    def update_search_results(self, *args):
        query = self.search_var.get()
        for widget in self.search_results_frame.winfo_children():
            widget.destroy()
        if query:
            results = self.search_programs(query)
            if results:
                ctk.CTkLabel(self.search_results_frame, text="Search Results", text_color=COLOR_TEXT_PRIMARY, font=('Inter', 11, 'bold')).pack(anchor=tk.W, padx=12, pady=(8, 4))
                for program in results:
                    btn = ctk.CTkButton(self.search_results_frame, text=program, anchor="w", fg_color=COLOR_BG_TERTIARY, hover_color=COLOR_BORDER, font=('Inter', 12), corner_radius=6, command=lambda p=program: [self.launch_program(p), self.toggle_start_menu(close_only=True)])
                    btn.pack(fill=tk.X, padx=8, pady=3)
            else:
                ctk.CTkLabel(self.search_results_frame, text="No results found", text_color=COLOR_TEXT_SECONDARY, font=('Inter', 12)).pack(pady=16)
            self.search_results_frame.pack(fill=tk.BOTH, expand=True)
            for widget in self.right_panel.winfo_children():
                if widget != self.search_results_frame:
                    widget.pack_forget()
        else:
            self.search_results_frame.pack_forget()
            for widget in self.right_panel.winfo_children():
                if widget != self.search_results_frame:
                    widget.pack()
    def search_programs(self, query):
        query = query.lower().strip()
        if not query:
            return []
        matches = []
        for name in PROGRAMS:
            if query in name.lower():
                matches.append(name)
        return matches
    def show_power_menu(self):
        # Create a small popup menu for power options
        menu = tk.Menu(self.root, tearoff=0, bg=COLOR_BG_SECONDARY, fg=COLOR_TEXT_PRIMARY)
        menu.add_command(label="Shutdown", command=self.shutdown_program)
        menu.add_command(label="Restart", command=self.restart_program)
        menu.add_command(label="Logout", command=self.logout)
        # Get mouse position
        x, y = self.root.winfo_pointerx(), self.root.winfo_pointery()
        menu.post(x, y - 100)
    def launch_program(self, name):
        if name in PROGRAMS:
            try:
                if name == "Media Viewer":
                    from tkinter import filedialog
                    file_path = filedialog.askopenfilename(filetypes=[("Media Files", "*.mp4 *.avi *.mkv")])
                    if not file_path:
                        return
                    # Assuming launch_media_viewer takes file_path and callbacks
                    # If not, adjust accordingly
                    win = PROGRAMS[name](self.root, file_path)
                else:
                    win = PROGRAMS[name](self.root)
                if win:
                    self.register_window(name, win)
            except:
                win = create_mock_app(name)(self.root)
                if win:
                    self.register_window(name, win)
        else:
            messagebox.showinfo("Info", "App not installed")
    def register_window(self, name, window):
        btn_frame = ctk.CTkFrame(self.running_apps_frame, fg_color=COLOR_BG_TERTIARY, width=40, height=40, corner_radius=10)
        btn_frame.pack(side=tk.LEFT, padx=4, pady=10)
        btn_frame.pack_propagate(False)
        icon_label = ctk.CTkLabel(btn_frame, text=ICONS.get(name, name[:3]), font=("Segoe UI Emoji", 20))
        icon_label.pack(expand=True)
        minimized = False
        def toggle_minimize():
            nonlocal minimized
            if minimized:
                window.deiconify()
                btn_frame.configure(fg_color=COLOR_BG_TERTIARY)
                minimized = False
                window.lift()
                window.focus_force()
            else:
                window.withdraw()
                btn_frame.configure(fg_color=COLOR_BORDER)
                minimized = True
        def focus_window(event=None):
            if minimized:
                toggle_minimize()
            else:
                window.lift()
                window.focus_force()
        def on_enter(e):
            btn_frame.configure(fg_color=COLOR_ACCENT_PRIMARY)
        def on_leave(e):
            if not minimized:
                btn_frame.configure(fg_color=COLOR_BG_TERTIARY)
            else:
                btn_frame.configure(fg_color=COLOR_BORDER)
        btn_frame.bind("<Button-1>", focus_window)
        btn_frame.bind("<Button-3>", lambda e: toggle_minimize())
        icon_label.bind("<Button-1>", focus_window)
        icon_label.bind("<Button-3>", lambda e: toggle_minimize())
        btn_frame.bind("<Enter>", on_enter)
        btn_frame.bind("<Leave>", on_leave)
        icon_label.bind("<Enter>", on_enter)
        icon_label.bind("<Leave>", on_leave)
        self.open_windows[window] = (btn_frame, minimized, icon_label)
        window.protocol("WM_DELETE_WINDOW", lambda: self._on_program_close(window))
        window.lift()
        window.focus_force()
    def _on_program_close(self, window):
        if window in self.open_windows:
            btn_frame, _, _ = self.open_windows[window]
            btn_frame.destroy()
            del self.open_windows[window]
        if window.winfo_exists():
            window.destroy()
    def update_clock(self):
        if not hasattr(self, 'clock_label') or not self.clock_label.winfo_exists():
            return
        try:
            current_time = time.strftime("%I:%M %p")
            current_date = time.strftime("%d.%m.%Y")
            self.clock_label.configure(text=current_time)
            if hasattr(self, 'taskbar_date_label') and self.taskbar_date_label.winfo_exists():
                self.taskbar_date_label.configure(text=current_date)
        except Exception: pass
        self.clock_update_id = self.root.after(1000, self.update_clock)
    def update_battery(self):
        if not hasattr(self, 'battery_label') or not self.battery_label.winfo_exists():
            return
        text = "🔋 --%"
        if BATTERY_ENABLED:
            try:
                battery = psutil.sensors_battery()
                if battery:
                    plugged = "⚡" if battery.power_plugged else ""
                    percent = int(battery.percent)
                    text = f"🔋 {percent}% {plugged}"
                    if not battery.power_plugged:
                        if percent <= 25 and percent > 10 and not self._warned_25:
                            self._warned_25 = True
                            play_sound("batterylow.wav")
                            messagebox.showwarning("Battery Low", "Battery is low (25%). Please plug in your charger.")
                        elif percent <= 10 and not self._warned_10:
                            self._warned_10 = True
                            play_sound("batterycritical.wav")
                            messagebox.showwarning("Battery Critical", "Battery is critically low (10%)! Plug in now to avoid shutdown.")
                        elif percent > 25:
                            self._warned_25 = False
                            self._warned_10 = False
                        elif percent > 10:
                            self._warned_10 = False
            except: pass
        try: self.battery_label.configure(text=text)
        except: pass
        self.battery_update_id = self.root.after(5000, self.update_battery)
    def logout(self):
        play_sound("logoff.wav")
        for window in list(self.open_windows):
            self._on_program_close(window)
        self.open_windows.clear()
        if self.clock_update_id: self.root.after_cancel(self.clock_update_id)
        if self.battery_update_id: self.root.after_cancel(self.battery_update_id)
        self.show_login_screen()
    def restart_program(self):
        play_sound("shutdownsound.wav")
        for window in list(self.open_windows):
            self._on_program_close(window)
        self.open_windows.clear()
        if self.clock_update_id: self.root.after_cancel(self.clock_update_id)
        if self.battery_update_id: self.root.after_cancel(self.battery_update_id)
        if self.shutdown_animation_id: self.root.after_cancel(self.shutdown_animation_id)
        self.show_shutdown_screen(restart=True)
    def shutdown_program(self):
        play_sound("shutdownsound.wav")
        for window in list(self.open_windows):
            self._on_program_close(window)
        self.open_windows.clear()
        if self.clock_update_id: self.root.after_cancel(self.clock_update_id)
        if self.battery_update_id: self.root.after_cancel(self.battery_update_id)
        if self.shutdown_animation_id: self.root.after_cancel(self.shutdown_animation_id)
        self.show_shutdown_screen(restart=False)
    def show_shutdown_screen(self, restart=False):
        # Stop clock updates
        if self.clock_update_id: self.root.after_cancel(self.clock_update_id)
        for widget in self.root.winfo_children(): widget.destroy()
        frame = ctk.CTkFrame(self.root, fg_color=COLOR_BG_PRIMARY)
        frame.pack(fill=tk.BOTH, expand=True)
        msg = "Restarting..." if restart else "Shutting Down..."
        ctk.CTkLabel(frame, text=msg, font=("Inter", 32, "bold"), text_color=COLOR_TEXT_PRIMARY).place(relx=0.5, rely=0.4, anchor=tk.CENTER)
        # Spinner Canvas
        self.spinner = Canvas(frame, width=100, height=100, bg=COLOR_BG_PRIMARY, highlightthickness=0)
        self.spinner.place(relx=0.5, rely=0.55, anchor=tk.CENTER)
        self.shutdown_angle = 0
        self.animate_shutdown(restart)
    def animate_shutdown(self, restart):
        if not hasattr(self, 'spinner') or not self.spinner.winfo_exists(): return
        self.spinner.delete("all")
        # Draw spinner dots
        cx, cy, r = 50, 50, 30
        for i in range(12):
            angle = math.radians(self.shutdown_angle + (i * 30))
            alpha = int(255 * (i / 12))
            color = f'#{alpha:02x}{alpha:02x}{alpha:02x}'
            x = cx + r * math.cos(angle)
            y = cy + r * math.sin(angle)
            self.spinner.create_oval(x-3, y-3, x+3, y+3, fill=color, outline=color)
        self.shutdown_angle = (self.shutdown_angle + 30) % 360
        # Continue animation or exit
        # For simulation, we just loop a bit then close
        if not hasattr(self, 'shutdown_start'): self.shutdown_start = time.time()
        if time.time() - self.shutdown_start > 3:
            if restart:
                python = sys.executable
                os.execl(python, python, *sys.argv)
            else:
                self.root.destroy()
        else:
            self.shutdown_animation_id = self.root.after(50, lambda: self.animate_shutdown(restart))
    def show_about(self):
        messagebox.showinfo("About", "youOS 8\n\nhardware module: ASUSTeK COMPUTER INC. ASUS TUF Gaming F15 FX506HC_FX506HC\n\nprocessor:11th Gen Intel® Core™ i7-11800H × 16\n\nmemory: 16 GB \n\n Disk: 2TB ")
    def show_all_programs(self):
        all_programs_window = tk.Toplevel(self.root)
        all_programs_window.title("All Programs")
        all_programs_window.attributes('-topmost', True)
        all_programs_window.geometry("380x480")
        all_programs_window.configure(bg=COLOR_BG_SECONDARY)
        scrollable_frame = ctk.CTkScrollableFrame(
            all_programs_window, fg_color=COLOR_BG_SECONDARY
        )
        scrollable_frame.pack(fill=tk.BOTH, expand=True)
        for name in sorted(PROGRAMS.keys()):
            btn = ctk.CTkButton(
                scrollable_frame, text=name, anchor=tk.W,
                fg_color=COLOR_BG_TERTIARY, hover_color=COLOR_BORDER,
                font=('Inter', 12), corner_radius=6,
                command=lambda p=name: (
                    self.launch_program(p),
                    all_programs_window.destroy()
                )
            )
            btn.pack(fill=tk.X, padx=6, pady=3)
if __name__ == "__main__":
    root = ctk.CTk()
    app = DesktopApp(root)
    root.mainloop()