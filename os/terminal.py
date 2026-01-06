import tkinter as tk
from tkinter import scrolledtext, ttk, messagebox # Added messagebox import
import subprocess
import os
import sys
import threading

class TerminalWindow:
    def __init__(self, parent):
        self.parent = parent
        self.terminal = None
        self.command_history = []
        self.history_index = 0
        self.on_close_external_handler = None # New attribute to store the external callback

    def create_terminal(self):
        # Create new terminal window
        self.terminal = tk.Toplevel(self.parent)
        self.terminal.title("youOS Terminal")
        self.terminal.geometry("800x600")

        # Configure dark theme
        bg_color = "#1e1e1e"
        fg_color = "#ffffff"
        entry_bg = "#333333"

        # Main frame
        main_frame = tk.Frame(self.terminal, bg=bg_color)
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Output text area
        self.output_text = scrolledtext.ScrolledText(
            main_frame,
            bg=bg_color,
            fg=fg_color,
            insertbackground=fg_color,
            font=("Consolas", 10),
            wrap=tk.WORD,
            state=tk.DISABLED # Start as DISABLED to prevent direct user typing
        )
        self.output_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Input area
        input_frame = tk.Frame(main_frame, bg=bg_color)
        input_frame.pack(fill=tk.X, padx=5, pady=(0, 5))

        # Current directory label
        current_dir = os.getcwd()
        self.dir_label = tk.Label(input_frame, text=f"{os.path.basename(current_dir)}>", bg=bg_color, fg="#00ff00", font=("Consolas", 10))
        self.dir_label.pack(side=tk.LEFT)

        self.command_entry = tk.Entry(
            input_frame,
            bg=entry_bg,
            fg=fg_color,
            insertbackground=fg_color,
            font=("Consolas", 10)
        )
        self.command_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        self.command_entry.focus_set()

        # Bind keys
        self.command_entry.bind("<Return>", self.execute_command)
        self.command_entry.bind("<Up>", self.handle_history)
        self.command_entry.bind("<Down>", self.handle_history)

        # Execute button
        execute_btn = tk.Button(
            input_frame,
            text="Execute",
            bg=entry_bg,
            fg=fg_color,
            command=self.execute_command,
            font=("Consolas", 9)
        )
        execute_btn.pack(side=tk.LEFT, padx=5)

        # WM_DELETE_WINDOW protocol will be set by open_terminal from DesktopApp
        # No need to set it here directly

        # Welcome message
        welcome_text = f"""youOS Terminal v1.0
Python {sys.version}
Current Directory: {os.getcwd()}
Type 'help' for available commands

"""
        self.insert_output(welcome_text, "#ffff00")

        # Make terminal resizable
        self.terminal.resizable(True, True)

        return self.terminal

    def _on_window_close_x_button(self):
        """This method is called when the user clicks the X button or
           when 'exit'/'quit' is typed. It triggers the external callback."""
        if self.on_close_external_handler:
            self.on_close_external_handler(self.terminal) # Call the external handler
        else:
            # Fallback if no specific callback (e.g., when testing terminal.py directly)
            if self.terminal and tk.Toplevel.winfo_exists(self.terminal):
                self.terminal.destroy()
        self.terminal = None # Clear reference

    def update_prompt(self):
        if self.dir_label:
            current_dir = os.getcwd()
            self.dir_label.config(text=f"{os.path.basename(current_dir)}>")

    def insert_output(self, text, color=None):
        """Insert text into output area with optional color"""
        if self.output_text:
            try:
                self.output_text.config(state=tk.NORMAL) # Enable writing
                if color:
                    self.output_text.tag_configure(color, foreground=color)
                    self.output_text.insert(tk.END, text, color)
                else:
                    self.output_text.insert(tk.END, text)
                self.output_text.see(tk.END)
                self.output_text.config(state=tk.DISABLED) # Disable writing again
                self.output_text.update_idletasks() # Ensure update is processed
            except Exception as e:
                # print(f"Error inserting output: {e}") # Keep commented for silent failure in desktop env
                pass

    def execute_command_async(self, command):
        """Execute command in a separate thread to prevent GUI freezing"""
        try:
            # Change to current directory for subprocess
            result = subprocess.run(
                command,
                shell=True,
                capture_output=True,
                text=True,
                cwd=os.getcwd(),
                timeout=30  # 30 second timeout
            )

            # Use self.terminal.after to update GUI from the main thread
            if result.stdout:
                self.terminal.after(0, self.insert_output, result.stdout)
            if result.stderr:
                self.terminal.after(0, self.insert_output, f"Error: {result.stderr}\n", "#ff6666")

        except subprocess.TimeoutExpired:
            self.terminal.after(0, self.insert_output, "Command timed out (30 seconds)\n", "#ff6666")
        except Exception as e:
            self.terminal.after(0, self.insert_output, f"Error executing command: {str(e)}\n", "#ff6666")


    def execute_command(self, event=None):
        if not self.terminal or not self.command_entry:
            return

        command = self.command_entry.get().strip()
        if not command:
            return

        # Add to history
        if command not in self.command_history:
            self.command_history.append(command)

        # Show the command that was entered
        self.insert_output(f"{os.path.basename(os.getcwd())}> {command}\n", "#00ff00")

        # Handle special commands
        if command.lower() in ["exit", "quit"]:
            self._on_window_close_x_button() # Call the unified close method
            return

        elif command.lower() == "clear":
            if self.output_text:
                self.output_text.config(state=tk.NORMAL)
                self.output_text.delete(1.0, tk.END)
                self.output_text.config(state=tk.DISABLED)

        elif command.lower() == "help":
            help_text = """youOS Terminal Commands:
-----------------------
help       - Show this help
clear      - Clear terminal
exit/quit  - Close terminal
ls/dir     - List files in current directory
cd <path>  - Change directory
pwd        - Show current directory
python     - Start Python interpreter (requires Python in PATH)
run <app>  - Launch application (DesktopApp integration)
history    - Show command history

You can also run any system command available on your OS.
"""
            self.insert_output(help_text, "#ffff00")

        elif command.lower().startswith("run "):
            app = command[4:].strip()
            self.insert_output(f"Launching {app}...\n", "#00ffff")
            try:
                # Use a specific custom event to communicate with DesktopApp
                self.parent.event_generate("<<LaunchProgramEvent>>", data=app)
            except Exception as e:
                self.insert_output(f"Could not launch {app}: {e}\n", "#ff6666")

        elif command.lower() in ["ls", "dir"]:
            try:
                files = os.listdir(".")
                self.insert_output("Directory contents:\n", "#ffff00")
                for f in sorted(files):
                    if os.path.isdir(f):
                        self.insert_output(f"  [DIR]  {f}\n", "#00ffff")
                    else:
                        self.insert_output(f"  [FILE] {f}\n")
            except Exception as e:
                self.insert_output(f"Error listing directory: {str(e)}\n", "#ff6666")

        elif command.lower() == "pwd":
            self.insert_output(f"{os.getcwd()}\n", "#00ffff")

        elif command.lower().startswith("cd "):
            path = command[3:].strip()
            if not path:
                path = os.path.expanduser("~")  # Go to home directory
            try:
                os.chdir(path)
                self.update_prompt()
                self.insert_output(f"Changed to: {os.getcwd()}\n", "#00ffff")
            except Exception as e:
                self.insert_output(f"Error changing directory: {str(e)}\n", "#ff6666")

        elif command.lower() == "cd":
            # Change to home directory
            try:
                os.chdir(os.path.expanduser("~"))
                self.update_prompt()
                self.insert_output(f"Changed to: {os.getcwd()}\n", "#00ffff")
            except Exception as e:
                self.insert_output(f"Error changing to home directory: {str(e)}\n", "#ff6666")

        elif command.lower() == "history":
            self.insert_output("Command History:\n", "#ffff00")
            for i, cmd in enumerate(self.command_history, 1):
                self.insert_output(f"  {i}. {cmd}\n")

        else:
            # Execute system command in separate thread
            thread = threading.Thread(target=self.execute_command_async, args=(command,))
            thread.daemon = True
            thread.start()

        # Clear input
        if self.command_entry:
            self.command_entry.delete(0, tk.END)
        self.history_index = len(self.command_history)

    def handle_history(self, event):
        """Handle up/down arrow keys for command history"""
        if not self.command_entry:
            return

        if event.keysym == "Up" and self.command_history:
            if self.history_index > 0:
                self.history_index -= 1
                self.command_entry.delete(0, tk.END)
                self.command_entry.insert(0, self.command_history[self.history_index])
        elif event.keysym == "Down" and self.command_history:
            if self.history_index < len(self.command_history) - 1:
                self.history_index += 1
                self.command_entry.delete(0, tk.END)
                self.command_entry.insert(0, self.command_history[self.history_index])
            else:
                self.history_index = len(self.command_history)
                self.command_entry.delete(0, tk.END)

# Global variable to track terminal instances
terminal_instances = []

def open_terminal(root, on_close_callback=None):
    """Create and open a new terminal window
    Args:
        root: The parent Tkinter root window.
        on_close_callback: A function provided by the parent (DesktopApp) to call
                           when the terminal window needs to be closed and unregistered
                           from the parent's taskbar. This function should accept the
                           Toplevel window as an argument.
    """
    global terminal_instances # <--- ADD THIS LINE

    terminal_obj = TerminalWindow(root)
    terminal_window = terminal_obj.create_terminal()

    # Pass the desktop app's on_close_callback to the TerminalWindow instance
    # so it can be called when 'exit'/'quit' is typed.
    # We also set the WM_DELETE_WINDOW protocol using this callback.
    if on_close_callback:
        terminal_obj.on_close_external_handler = on_close_callback
        terminal_window.protocol("WM_DELETE_WINDOW", lambda: on_close_callback(terminal_window))
    else:
        # Fallback if no callback provided (e.g., when testing terminal.py directly)
        terminal_window.protocol("WM_DELETE_WINDOW", terminal_obj._on_window_close_x_button)

    terminal_instances.append(terminal_obj)
    # Clean up closed instances (optional, for tracking only)
    # The current cleanup line also reassigns the global variable, so 'global' is critical
    terminal_instances = [t for t in terminal_instances if t.terminal is not None and (isinstance(t.terminal, tk.Toplevel)) and t.terminal.winfo_exists()]

    return terminal_window


# Example usage for testing terminal.py directly
if __name__ == "__main__":
    root = tk.Tk()
    root.title("Test Application")
    root.geometry("300x200")

    # Simple placeholder close callback for testing
    def test_close_callback(window_to_close):
        print(f"Test close callback received: {window_to_close}")
        if window_to_close.winfo_exists():
            window_to_close.destroy()

    # Button to open terminal
    open_btn = tk.Button(root, text="Open Terminal", command=lambda: open_terminal(root, test_close_callback))
    open_btn.pack(pady=50)

    # Bind a general event for 'run' command testing
    def handle_launch_program_event(event):
        program_name = event.data # Access data from custom event
        messagebox.showinfo("Launch Request", f"Would launch '{program_name}'")

    root.bind("<<LaunchProgramEvent>>", handle_launch_program_event)

    root.mainloop()