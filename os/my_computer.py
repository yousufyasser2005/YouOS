import tkinter as tk
from tkinter import ttk, messagebox
import os
import shutil
import psutil  # For additional system info if needed
from PIL import Image, ImageTk
import pygame
import customtkinter as ctk
import subprocess
import json
import re
from datetime import datetime
from arabic_reshaper import reshape
from bidi.algorithm import get_display

# Set CustomTkinter theme
ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

# Assuming media_viewer.py exists with launch_media_viewer function
try:
    from media_viewer import launch_media_viewer
except ImportError:
    def launch_media_viewer(root, path):
        messagebox.showerror("Error", "Media viewer module not found")

ASSETS_DIR = os.path.join(os.path.dirname(__file__), "assets")
os.makedirs(ASSETS_DIR, exist_ok=True)  # Ensure ASSETS_DIR exists

pygame.mixer.init()

def play_sound(filename):
    try:
        path = os.path.join(ASSETS_DIR, f"{filename}.wav")
        if not os.path.exists(path):
            print(f"Sound file {path} not found")
            return
        pygame.mixer.music.load(path)
        pygame.mixer.music.play()
    except Exception as e:
        print(f"Error playing sound: {e}")

def get_file_properties(filepath):
    props = {}
    try:
        props["type"] = os.path.splitext(filepath)[1][1:] or "Unknown"
        props["size"] = os.path.getsize(filepath)
        props["modified"] = datetime.fromtimestamp(os.path.getmtime(filepath)).strftime('%Y-%m-%d %H:%M:%S')
    except Exception:
        props = {"type": "N/A", "size": "N/A", "modified": "N/A"}
    return props

def get_all_block_devices():
    try:
        output = subprocess.check_output([
            'lsblk', '-o', 'NAME,FSTYPE,SIZE,MOUNTPOINT,TYPE,LABEL', '-J'
        ]).decode()
        data = json.loads(output)
        return data.get('blockdevices', [])
    except Exception as e:
        print(f"Error getting block devices: {e}")
        return []

class MyComputer:
    def __init__(self, root):
        self.root = ctk.CTkToplevel(root)
        self.root.title("My Computer")
        self.root.geometry("1000x600")
        self.main_root = root
        self.closed = False
        self.clipboard_file = None
        self.current_dir = os.path.expanduser("~")  # Default to home directory
        self.history = [self.current_dir]  # Initialize history
        self.history_index = 0  # Current position in history
        self.file_mapping = {}  # Map display text to original filename
        
        # Styling for ttk widgets with Arabic support
        self.style = ttk.Style()
        self.style.configure("Treeview", font=('Arial', 10), background="#2a2d2e", foreground="white", fieldbackground="#2a2d2e")
        self.style.configure("Treeview.Heading", font=('Arial', 10, 'bold'), background="#1f538d", foreground="white")
        
        self.create_widgets()
        self.load_disks()
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def create_widgets(self):
        # Main container
        main_frame = ctk.CTkFrame(self.root)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Left pane - Disk Navigation
        left_pane = ctk.CTkFrame(main_frame, width=300)
        left_pane.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 5))
        left_pane.pack_propagate(False)
        
        ctk.CTkLabel(left_pane, text="Disk Navigation", font=('Arial', 12, 'bold')).pack(pady=5)
        
        # Refresh button
        ctk.CTkButton(left_pane, text="Refresh", command=self.load_disks, font=('Arial', 10)).pack(pady=5)
        
        self.tree = ttk.Treeview(left_pane)
        self.tree.pack(fill=tk.BOTH, expand=True)
        self.tree.bind("<<TreeviewSelect>>", self.on_tree_select)
        
        # Right pane - File View
        right_pane = ctk.CTkFrame(main_frame)
        right_pane.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)
        
        # Navigation buttons frame
        nav_frame = ctk.CTkFrame(right_pane)
        nav_frame.pack(fill=tk.X, pady=5)
        ctk.CTkButton(nav_frame, text="Back", command=self.go_back, font=('Arial', 10), width=60).pack(side=tk.LEFT, padx=2)
        ctk.CTkButton(nav_frame, text="Forward", command=self.go_forward, font=('Arial', 10), width=60).pack(side=tk.LEFT, padx=2)
        ctk.CTkButton(nav_frame, text="Up", command=self.go_up, font=('Arial', 10), width=60).pack(side=tk.LEFT, padx=2)
        
        # File list header
        ctk.CTkLabel(right_pane, text="Files and Folders", font=('Arial', 12, 'bold')).pack(pady=5)
        
        # File list
        self.file_list = ttk.Treeview(right_pane, columns=("Size", "Type", "Modified"), selectmode="browse")
        self.file_list.pack(fill=tk.BOTH, expand=True)
        
        # Configure columns
        self.file_list.heading("#0", text="Name", anchor=tk.W)
        self.file_list.heading("Size", text="Size", anchor=tk.W)
        self.file_list.heading("Type", text="Type", anchor=tk.W)
        self.file_list.heading("Modified", text="Modified", anchor=tk.W)
        
        self.file_list.column("#0", width=300)
        self.file_list.column("Size", width=100)
        self.file_list.column("Type", width=100)
        self.file_list.column("Modified", width=150)
        
        self.file_list.bind("<Double-1>", self.open_selected)
        
        # Context menu
        self.context_menu = tk.Menu(self.root, tearoff=0, bg="#2a2d2e", fg="white")
        self.context_menu.add_command(label="Open", command=self.open_selected)
        self.context_menu.add_command(label="Copy", command=self.copy_file)
        self.context_menu.add_command(label="Paste", command=self.paste_file)
        self.context_menu.add_command(label="Delete", command=self.delete_file)
        self.context_menu.add_command(label="Properties", command=self.show_properties)
        self.context_menu.add_command(label="Mount", command=self.mount_selected)
        self.context_menu.add_command(label="Unmount", command=self.unmount_selected)
        
        self.file_list.bind("<Button-3>", self.show_context_menu)
        
        # Status bar
        self.status_bar = ctk.CTkFrame(self.root)
        self.status_bar.pack(fill=tk.X, padx=5, pady=(0, 5))
        self.status_label = ctk.CTkLabel(self.status_bar, text="Ready", font=('Arial', 10))
        self.status_label.pack(side=tk.LEFT)

        # Buttons frame below file list
        button_frame = ctk.CTkFrame(right_pane)
        button_frame.pack(fill=tk.X, pady=5)
        ctk.CTkButton(button_frame, text="Delete", command=self.delete_file, font=('Arial', 10)).pack(side=tk.LEFT, padx=5)
        ctk.CTkButton(button_frame, text="Cut", command=self.cut_file, font=('Arial', 10)).pack(side=tk.LEFT, padx=5)
        ctk.CTkButton(button_frame, text="Copy", command=self.copy_file, font=('Arial', 10)).pack(side=tk.LEFT, padx=5)

    def load_disks(self):
        """Load all connected disks, partitions, and special folders"""
        # Clear existing items
        for item in self.tree.get_children():
            self.tree.delete(item)
        
        # Add block devices
        disk_root = self.tree.insert("", "end", text="This PC", open=True)
        block_devices = get_all_block_devices()
        self.insert_devices(disk_root, block_devices)
        
        # Add special folders
        special_folders = self.tree.insert("", "end", text="Quick Access", open=True)
        for name, path in [
            ("Desktop", os.path.expanduser("~/Desktop")),
            ("Documents", os.path.expanduser("~/Documents")),
            ("Downloads", os.path.expanduser("~/Downloads")),
            ("Pictures", os.path.expanduser("~/Pictures"))
        ]:
            if os.path.exists(path):
                self.tree.insert(special_folders, "end", text=name, values=("", "", path, "folder"))

    def insert_devices(self, parent, devices):
        for dev in devices:
            text = dev['name']
            if dev['label']:
                # Reshape Arabic label for RTL display
                reshaped_label = get_display(reshape(dev['label'])) if dev['label'] else ""
                text += f" ({reshaped_label})" if reshaped_label else ""
            text += f" - {dev['size']}"
            if dev['fstype']:
                text += f" [{dev['fstype'].upper()}]"
            if dev['mountpoint']:
                text += f" (mounted at {dev['mountpoint']})"
            item = self.tree.insert(
                parent, "end", text=text,
                values=(dev['name'], dev['fstype'] or '', dev['mountpoint'] or '', dev['type'])
            )
            if 'children' in dev:
                self.insert_devices(item, dev['children'])

    def on_tree_select(self, event):
        """Handle selection in the tree view"""
        selected = self.tree.focus()
        if not selected:
            return
        values = self.tree.item(selected, "values")
        if len(values) < 3:
            return  # Likely a root item
        name, fstype, mountpoint, dev_type = values
        
        if mountpoint:  # Already mounted or a special folder
            self.update_history(mountpoint)
            self.load_directory(mountpoint)
        elif dev_type == 'part' and fstype:  # Unmounted partition
            self.status_label.configure(text=f"Selected: {name} ({fstype}) - Unmounted")
            self.file_list.delete(*self.file_list.get_children())  # Clear file list

    def update_history(self, path):
        """Update navigation history"""
        if self.history_index < len(self.history) - 1:
            self.history = self.history[:self.history_index + 1]
        if not self.history or self.history[-1] != path:
            self.history.append(path)
            self.history_index = len(self.history) - 1

    def go_back(self):
        """Navigate to the previous directory in history"""
        if self.history_index > 0:
            self.history_index -= 1
            self.load_directory(self.history[self.history_index])

    def go_forward(self):
        """Navigate to the next directory in history"""
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
            self.load_directory(self.history[self.history_index])

    def go_up(self):
        """Navigate to the parent directory"""
        parent_dir = os.path.dirname(self.current_dir)
        if parent_dir and parent_dir != self.current_dir:
            self.update_history(parent_dir)
            self.load_directory(parent_dir)

    def mount_selected(self, selected=None):
        if not selected:
            selected = self.tree.focus()
        if not selected:
            return
        values = self.tree.item(selected, "values")
        if len(values) < 3:
            return
        name, fstype, mountpoint, dev_type = values
        if mountpoint:
            messagebox.showinfo("Info", f"Device {name} is already mounted at {mountpoint}")
            return
        if dev_type != 'part':
            messagebox.showerror("Error", "Only partitions can be mounted")
            return
        
        device = f"/dev/{name}"
        try:
            output = subprocess.check_output(['udisksctl', 'mount', '-b', device]).decode()
            match = re.search(r'at (.+)\.', output)
            if match:
                new_mountpoint = match.group(1)
                new_text = self.tree.item(selected, "text").split(" (mounted at")[0] + f" (mounted at {new_mountpoint})"
                self.tree.item(selected, text=new_text, values=(name, fstype, new_mountpoint, dev_type))
                self.update_history(new_mountpoint)
                self.load_directory(new_mountpoint)
                self.status_label.configure(text=f"Mounted {name} at {new_mountpoint}")
            else:
                raise ValueError("Could not parse mount point")
        except subprocess.CalledProcessError as e:
            play_sound("error")
            messagebox.showerror("Mount Error", f"Failed to mount {device}: {str(e)}")
        except Exception as e:
            play_sound("error")
            messagebox.showerror("Mount Error", f"Unexpected error mounting {device}: {str(e)}")

    def unmount_selected(self, selected=None):
        if not selected:
            selected = self.tree.focus()
        if not selected:
            return
        values = self.tree.item(selected, "values")
        if len(values) < 3:
            return
        name, fstype, mountpoint, dev_type = values
        if not mountpoint:
            messagebox.showinfo("Info", f"Device {name} is not mounted")
            return
        if dev_type != 'part':
            messagebox.showerror("Error", "Only partitions can be unmounted")
            return
        
        device = f"/dev/{name}"
        try:
            subprocess.check_call(['udisksctl', 'unmount', '-b', device])
            new_text = self.tree.item(selected, "text").split(" (mounted at")[0]
            self.tree.item(selected, text=new_text, values=(name, fstype, '', dev_type))
            self.file_list.delete(*self.file_list.get_children())  # Clear file list
            self.status_label.configure(text=f"Unmounted {name}")
        except subprocess.CalledProcessError as e:
            play_sound("error")
            messagebox.showerror("Unmount Error", f"Failed to unmount {device}: {str(e)}")
        except Exception as e:
            play_sound("error")
            messagebox.showerror("Unmount Error", f"Unexpected error unmounting {device}: {str(e)}")

    def load_directory(self, path):
        """Load directory contents into the file list"""
        if not os.path.isdir(path):
            play_sound("error")
            messagebox.showerror("Error", "Invalid directory")
            return

        self.current_dir = path  # Update current directory
        # Clear existing items and mapping
        for item in self.file_list.get_children():
            self.file_list.delete(item)
        self.file_mapping.clear()
        
        # Add parent directory
        parent = os.path.dirname(path)
        if parent != path:  # Not root directory
            self.file_list.insert("", "end", text="..", values=("", "Folder", ""))
            self.file_mapping[".."] = ".."
        
        # Add directories
        try:
            for item in sorted(os.listdir(path)):
                full_path = os.path.join(path, item)
                if os.path.isdir(full_path):
                    # Reshape Arabic text for RTL display
                    reshaped_item = get_display(reshape(item)) if any(c >= '\u0600' and c <= '\u06FF' for c in item) else item
                    print(f"Original: {item}, Reshaped: {reshaped_item}")  # Debug
                    iid = self.file_list.insert("", "end", text=reshaped_item, values=("", "Folder", ""))
                    self.file_mapping[reshaped_item] = item
            
            # Add files
            for item in sorted(os.listdir(path)):
                full_path = os.path.join(path, item)
                if os.path.isfile(full_path):
                    props = get_file_properties(full_path)
                    size = f"{props['size']//1024} KB" if props['size'] < 1024*1024 else f"{props['size']//(1024*1024)} MB"
                    # Reshape Arabic text for RTL display
                    reshaped_item = get_display(reshape(item)) if any(c >= '\u0600' and c <= '\u06FF' for c in item) else item
                    print(f"Original: {item}, Reshaped: {reshaped_item}")  # Debug
                    iid = self.file_list.insert("", "end", text=reshaped_item, 
                                              values=(size, props['type'], props['modified']))
                    self.file_mapping[reshaped_item] = item
            
            self.status_label.configure(text=f"Location: {path}")
        except PermissionError:
            play_sound("error")
            messagebox.showerror("Error", "Access denied to this location")
        except Exception as e:
            play_sound("error")
            messagebox.showerror("Error", f"Failed to load directory: {e}")

    def get_selected_path(self):
        """Get full path of selected item using original filename"""
        selected = self.file_list.focus()
        if not selected:
            return None
        
        item_text = self.file_list.item(selected, "text")
        
        if item_text == "..":
            return os.path.dirname(self.current_dir)
        
        original_item = self.file_mapping.get(item_text, item_text)
        print(f"Selected: {item_text}, Original: {original_item}, Path: {os.path.join(self.current_dir, original_item)}")  # Enhanced Debug
        return os.path.join(self.current_dir, original_item)

    def open_selected(self, event=None):
        path = self.get_selected_path()
        if not path:
            return
            
        if os.path.isdir(path):
            self.update_history(path)
            self.load_directory(path)
        else:
            ext = os.path.splitext(path)[1].lower()
            print(f"Opening file: {path}, Extension: {ext}")  # Debug
            # Open media files (video, audio, images) in media_viewer
            if ext in [".mp4", ".mp3", ".avi", ".mkv", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp"]:
                try:
                    launch_media_viewer(self.root, path)
                except Exception as e:
                    play_sound("error")
                    messagebox.showerror("Error", f"Failed to open media: {e}")
            else:
                # Open other files with default system application
                try:
                    subprocess.Popen(
                        ['xdg-open', path],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL
                    )
                except Exception as e:
                    play_sound("error")
                    messagebox.showerror("Error", f"Cannot open this file: {e}")

    def show_context_menu(self, event):
        """Show right-click context menu"""
        item = self.file_list.identify_row(event.y)
        if item:
            self.file_list.selection_set(item)
            try:
                self.context_menu.tk_popup(event.x_root, event.y_root)
            finally:
                self.context_menu.grab_release()

    def copy_file(self):
        path = self.get_selected_path()
        if path and os.path.exists(path):
            self.clipboard_file = path
            self.status_label.configure(text=f"Copied: {os.path.basename(path)}")

    def paste_file(self):
        if not self.clipboard_file:
            play_sound("error")
            messagebox.showerror("Error", "Nothing to paste")
            return

        dest = os.path.join(self.current_dir, os.path.basename(self.clipboard_file))
        
        if os.path.exists(dest):
            play_sound("error")
            messagebox.showerror("Error", "File already exists")
            return

        try:
            if os.path.isdir(self.clipboard_file):
                shutil.copytree(self.clipboard_file, dest)
            else:
                shutil.copy2(self.clipboard_file, dest)
            if self.status_label.cget("text").startswith("Cut:"):
                os.remove(self.clipboard_file) if os.path.isfile(self.clipboard_file) else shutil.rmtree(self.clipboard_file)
            self.load_directory(self.current_dir)
            self.status_label.configure(text=f"Location: {self.current_dir}")
        except Exception as e:
            play_sound("error")
            messagebox.showerror("Error", f"Failed to paste file:\n{e}")

    def delete_file(self):
        path = self.get_selected_path()
        if not path:
            return
            
        play_sound("question")
        if messagebox.askyesno("Confirm", "Permanently delete this item?"):
            try:
                if os.path.isdir(path):
                    shutil.rmtree(path)
                else:
                    os.remove(path)
                self.load_directory(os.path.dirname(path) if path == self.current_dir else self.current_dir)
            except Exception as e:
                play_sound("error")
                messagebox.showerror("Error", f"Failed to delete:\n{e}")

    def cut_file(self):
        path = self.get_selected_path()
        if path and os.path.exists(path):
            self.clipboard_file = path
            self.status_label.configure(text=f"Cut: {os.path.basename(path)}")

    def show_properties(self):
        path = self.get_selected_path()
        if not path:
            return
            
        props = get_file_properties(path)
        info = f"Name: {os.path.basename(path)}\n"
        info += f"Location: {os.path.dirname(path)}\n"
        info += f"Type: {props['type']}\n"
        info += f"Size: {props['size']} bytes\n"
        info += f"Modified: {props['modified']}"
        
        messagebox.showinfo("Properties", info)

    def on_close(self):
        """Clean up when window is closed"""
        if not self.closed:
            self.closed = True
            try:
                if hasattr(self.main_root, 'desktop_app'):
                    desktop_app = self.main_root.desktop_app
                    if self.root in desktop_app.open_windows:
                        btn, _ = desktop_app.open_windows[self.root]
                        btn.destroy()
                        del desktop_app.open_windows[self.root]
            finally:
                self.root.destroy()

def open_my_computer(root):
    computer = MyComputer(root)
    if hasattr(root, 'desktop_app'):
        root.desktop_app.register_window("My Computer", computer.root)
    return computer.root