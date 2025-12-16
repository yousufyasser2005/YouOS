# text_editor.py
import tkinter as tk
from tkinter import filedialog, messagebox

def open_text_editor(parent):
    window = tk.Toplevel(parent)
    window.title("Text editor")
    window.geometry("600x400")
    window.current_file = None

    text_area = tk.Text(window, wrap="word", font=("Consolas", 12))
    text_area.pack(fill="both", expand=True, padx=10, pady=10)

    def new_file():
        text_area.delete(1.0, tk.END)
        window.current_file = None
        window.title("Text Manager - New File")

    def open_file():
        file_path = filedialog.askopenfilename(
            defaultextension=".txt",
            filetypes=[("Text Files", "*.txt")]
        )
        if file_path:
            with open(file_path, "r") as file:
                text_area.delete(1.0, tk.END)
                text_area.insert(tk.END, file.read())
            window.current_file = file_path
            window.title(f"Text Manager - {file_path}")

    def save_file():
        if window.current_file:
            with open(window.current_file, "w") as file:
                file.write(text_area.get(1.0, tk.END))
            messagebox.showinfo("Saved", "File saved successfully.")
        else:
            save_file_as()

    def save_file_as():
        file_path = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("Text Files", "*.txt")]
        )
        if file_path:
            with open(file_path, "w") as file:
                file.write(text_area.get(1.0, tk.END))
            window.current_file = file_path
            window.title(f"Text Manager - {file_path}")
            messagebox.showinfo("Saved", "File saved successfully.")

    # Menu
    menu_bar = tk.Menu(window)
    file_menu = tk.Menu(menu_bar, tearoff=0)
    file_menu.add_command(label="New", command=new_file)
    file_menu.add_command(label="Open", command=open_file)
    file_menu.add_command(label="Save", command=save_file)
    file_menu.add_command(label="Save As", command=save_file_as)
    file_menu.add_separator()
    file_menu.add_command(label="Exit", command=window.destroy)

    menu_bar.add_cascade(label="File", menu=file_menu)
    window.config(menu=menu_bar)

    return window

