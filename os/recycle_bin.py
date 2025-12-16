import tkinter as tk
from tkinter import messagebox, ttk
import os
import shutil
import pygame
import threading

# المجلدات
TRASH_DIR = os.path.join(os.path.dirname(__file__), "recycle_bin")
USER_FILES_DIR = os.path.join(os.path.dirname(__file__), "user files")
ASSETS_DIR = os.path.join(os.path.dirname(__file__), "assets")
os.makedirs(TRASH_DIR, exist_ok=True)

# تهيئة الصوت
pygame.mixer.init()

def play_success_sound():
    def play():
        try:
            path = os.path.join(ASSETS_DIR, "success.wav")
            sound = pygame.mixer.Sound(path)
            sound.play()
        except Exception as e:
            print(f"Error playing success sound: {e}")
    threading.Thread(target=play, daemon=True).start()

def move_to_trash(filepath):
    """نقل ملف إلى سلة المحذوفات"""
    if os.path.exists(filepath):
        filename = os.path.basename(filepath)
        destination = os.path.join(TRASH_DIR, filename)
        shutil.move(filepath, destination)

def open_recycle_bin(root):
    win = tk.Toplevel(root)
    win.title("Recycle Bin")
    win.geometry("600x400")

    tk.Label(win, text="Deleted Files", font=("Arial", 14)).pack(pady=10)

    tree = ttk.Treeview(win, columns=("Name", "Full Path"), show='headings')
    tree.heading("Name", text="Filename")
    tree.heading("Full Path", text="Path")
    tree.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

    def refresh():
        for row in tree.get_children():
            tree.delete(row)
        for file in os.listdir(TRASH_DIR):
            full_path = os.path.join(TRASH_DIR, file)
            tree.insert('', 'end', values=(file, full_path))

    def restore_file():
        selected = tree.selection()
        if not selected:
            return
        item = tree.item(selected[0])
        filename, path = item['values']

        destination = os.path.join(USER_FILES_DIR, filename)

        if os.path.exists(destination):
            messagebox.showwarning("Restore", f"{filename} already exists in user files.")
            return

        try:
            shutil.move(path, destination)
            refresh()
            messagebox.showinfo("Restored", f"{filename} restored successfully")
            play_success_sound()
        except Exception as e:
            messagebox.showerror("Restore Error", f"Could not restore {filename}: {e}")

    def delete_permanently():
        selected = tree.selection()
        if not selected:
            return
        item = tree.item(selected[0])
        filename, path = item['values']
        try:
            os.remove(path)
            refresh()
            messagebox.showinfo("Deleted", f"{filename} deleted permanently")
            play_success_sound()
        except Exception as e:
            messagebox.showerror("Error", f"Could not delete {filename}: {e}")

    def empty_trash():
        try:
            for file in os.listdir(TRASH_DIR):
                os.remove(os.path.join(TRASH_DIR, file))
            refresh()
            messagebox.showinfo("Recycle Bin", "Recycle Bin emptied")
            play_success_sound()
        except Exception as e:
            messagebox.showerror("Error", f"Could not empty recycle bin: {e}")

    btn_frame = tk.Frame(win)
    btn_frame.pack(pady=10)

    tk.Button(btn_frame, text="Restore", command=restore_file).grid(row=0, column=0, padx=10)
    tk.Button(btn_frame, text="Delete", command=delete_permanently).grid(row=0, column=1, padx=10)
    tk.Button(btn_frame, text="Empty Bin", command=empty_trash).grid(row=0, column=2, padx=10)

    refresh()
    return win
