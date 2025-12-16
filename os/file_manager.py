import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
import os
import shutil
import pygame

# ========== إعداد الصوت ==========
pygame.mixer.init()
ASSETS_DIR = os.path.join(os.path.dirname(__file__), "assets")

def play_error_sound():
    try:
        pygame.mixer.Sound(os.path.join(ASSETS_DIR, "error.wav")).play()
    except Exception as e:
        print("Error playing error sound:", e)

def play_success_sound():
    try:
        pygame.mixer.Sound(os.path.join(ASSETS_DIR, "success.wav")).play()
    except Exception as e:
        print("Error playing success sound:", e)

# ========== الإعدادات العامة ==========
ROOT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "user files")
RECYCLE_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recycle_bin")
os.makedirs(RECYCLE_BIN, exist_ok=True)

current_path = ROOT_DIR
clipboard = {
    "path": None,
    "action": None
}

# ========== الدوال ==========
def get_file_list(path):
    files = []
    for filename in os.listdir(path):
        file_path = os.path.join(path, filename)
        if os.path.isfile(file_path):
            size_kb = os.path.getsize(file_path) // 1024
            ext = os.path.splitext(filename)[1].lower()
            file_type = "Text" if ext in [".txt", ".md"] else "Image" if ext in [".jpg", ".png"] else "Other"
            files.append({
                "name": filename,
                "type": file_type,
                "size": f"{size_kb} KB" if size_kb > 0 else "1 KB"
            })
        elif os.path.isdir(file_path):
            files.append({
                "name": filename,
                "type": "Folder",
                "size": "-"
            })
    return files

def refresh_file_table(tree, path_label):
    tree.delete(*tree.get_children())
    path_label.config(text=f"Path: {current_path}")
    for file in get_file_list(current_path):
        tree.insert('', 'end', values=(file["name"], file["type"], file["size"]))

def open_item(tree, path_label):
    global current_path
    selected = tree.selection()
    if not selected:
        return
    item = selected[0]
    name, ftype, _ = tree.item(item, 'values')
    target_path = os.path.join(current_path, name)

    if ftype == "Folder":
        current_path = target_path
        refresh_file_table(tree, path_label)
    else:
        messagebox.showinfo("Open File", f"Opening file: {name}")

def delete_item(tree, path_label):
    selected = tree.selection()
    if not selected:
        return
    item = selected[0]
    name = tree.item(item, 'values')[0]
    file_path = os.path.join(current_path, name)

    try:
        if os.path.isdir(file_path):
            os.rmdir(file_path)
        else:
            os.remove(file_path)
        refresh_file_table(tree, path_label)
        play_success_sound()
    except Exception as e:
        play_error_sound()
        messagebox.showerror("Error", f"Cannot delete: {e}")

def move_to_recycle_bin(tree, path_label):
    selected = tree.selection()
    if not selected:
        return
    item = selected[0]
    name = tree.item(item, 'values')[0]
    file_path = os.path.join(current_path, name)
    target_path = os.path.join(RECYCLE_BIN, name)

    try:
        if os.path.exists(target_path):
            play_error_sound()
            messagebox.showwarning("Recycle Bin", f"{name} already exists in Recycle Bin.")
            return

        shutil.move(file_path, target_path)
        refresh_file_table(tree, path_label)
        play_success_sound()
    except Exception as e:
        play_error_sound()
        messagebox.showerror("Error", f"Move failed: {e}")

def create_new_folder(tree, path_label):
    folder_name = simpledialog.askstring("New Folder", "Enter folder name:")
    if folder_name:
        folder_path = os.path.join(current_path, folder_name)
        try:
            os.makedirs(folder_path)
            refresh_file_table(tree, path_label)
            play_success_sound()
        except Exception as e:
            play_error_sound()
            messagebox.showerror("Error", f"Could not create folder: {e}")

def go_back(tree, path_label):
    global current_path
    if current_path != ROOT_DIR:
        current_path = os.path.dirname(current_path)
        refresh_file_table(tree, path_label)

def copy_item(tree):
    selected = tree.selection()
    if selected:
        name = tree.item(selected[0], 'values')[0]
        clipboard["path"] = os.path.join(current_path, name)
        clipboard["action"] = "copy"

def cut_item(tree):
    selected = tree.selection()
    if selected:
        name = tree.item(selected[0], 'values')[0]
        clipboard["path"] = os.path.join(current_path, name)
        clipboard["action"] = "cut"

def paste_item(tree, path_label):
    if not clipboard["path"] or not os.path.exists(clipboard["path"]):
        play_error_sound()
        messagebox.showwarning("Paste", "Nothing to paste.")
        return

    src = clipboard["path"]
    dst = os.path.join(current_path, os.path.basename(src))

    try:
        if os.path.isdir(src):
            if os.path.exists(dst):
                play_error_sound()
                messagebox.showerror("Error", "Folder already exists.")
                return
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)

        if clipboard["action"] == "cut":
            if os.path.isdir(src):
                shutil.rmtree(src)
            else:
                os.remove(src)
            clipboard["path"] = None
            clipboard["action"] = None

        refresh_file_table(tree, path_label)
        play_success_sound()
    except Exception as e:
        play_error_sound()
        messagebox.showerror("Error", f"Paste failed: {e}")

# ========== نافذة البرنامج ==========
def open_file_explorer(parent):
    global current_path
    current_path = ROOT_DIR

    win = tk.Toplevel(parent)
    win.title("File Manager")
    win.geometry("750x400")

    path_label = tk.Label(win, text=f"Path: {current_path}", anchor="w")
    path_label.pack(fill="x")

    tree = ttk.Treeview(win, columns=("Name", "Type", "Size"), show="headings")
    tree.heading("Name", text="Name")
    tree.heading("Type", text="Type")
    tree.heading("Size", text="Size")
    tree.pack(fill="both", expand=True, pady=10)

    btn_frame = tk.Frame(win)
    btn_frame.pack(pady=5)

    tk.Button(btn_frame, text="Open", command=lambda: open_item(tree, path_label)).grid(row=0, column=0, padx=5)
    tk.Button(btn_frame, text="Delete", command=lambda: delete_item(tree, path_label)).grid(row=0, column=1, padx=5)
    tk.Button(btn_frame, text="New Folder", command=lambda: create_new_folder(tree, path_label)).grid(row=0, column=2, padx=5)
    tk.Button(btn_frame, text="Back", command=lambda: go_back(tree, path_label)).grid(row=0, column=3, padx=5)
    tk.Button(btn_frame, text="Copy", command=lambda: copy_item(tree)).grid(row=0, column=4, padx=5)
    tk.Button(btn_frame, text="Cut", command=lambda: cut_item(tree)).grid(row=0, column=5, padx=5)
    tk.Button(btn_frame, text="Paste", command=lambda: paste_item(tree, path_label)).grid(row=0, column=6, padx=5)
    tk.Button(btn_frame, text="Move to Recycle Bin", command=lambda: move_to_recycle_bin(tree, path_label)).grid(row=0, column=7, padx=5)

    refresh_file_table(tree, path_label)
    return win

