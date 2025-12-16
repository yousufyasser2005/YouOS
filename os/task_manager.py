import tkinter as tk
from tkinter import ttk, messagebox
import psutil
import threading
import time
import os
import signal
import subprocess
import sys

class TaskManager:
    def __init__(self, root, program_launcher=None, open_windows=None):
        self.root = root
        self.program_launcher = program_launcher or {}
        self.open_windows = open_windows or {}
        self.refresh_interval = 2000  # milliseconds
        self.auto_refresh = True
        self._refresh_scheduled = False
        
        # Desktop environment programs - these are the ones we want to track
        self.desktop_programs = {
            "Text Editor": "text_editor.py",
            "Settings": "settings.py", 
            "File Manager": "file_manager.py",
            "Media Viewer": "media_viewer.py",
            "Yousuf Browser": "yousuf_browser.py",
            "My Computer": "my_computer.py",
            "Calculator": "calculator.py",
            "Chess": "chess_game.py",
            "X-O": "xo_game.py",
            "Task Manager": "task_manager.py",
            "Recycle Bin": "recycle_bin.py"
        }
        
        # Get the current script directory to identify our processes
        self.script_dir = os.path.dirname(os.path.abspath(__file__))
        
        self.setup_ui()
        self.refresh_processes()
        self.start_auto_refresh()
    
    def setup_ui(self):
        self.win = tk.Toplevel(self.root)
        self.win.title("Task Manager - Desktop Environment")
        self.win.geometry("800x600")
        self.win.resizable(True, True)
        
        # Create notebook for tabs
        self.notebook = ttk.Notebook(self.win)
        self.notebook.pack(fill='both', expand=True, padx=5, pady=5)
        
        # Processes tab
        self.processes_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.processes_frame, text="Desktop Programs")
        
        # Performance tab (basic)
        self.performance_frame = ttk.Frame(self.notebook)
        self.notebook.add(self.performance_frame, text="Performance")
        
        self.setup_processes_tab()
        self.setup_performance_tab()
        
        # Status bar
        self.status_bar = tk.Label(self.win, text="Ready", relief=tk.SUNKEN, anchor=tk.W)
        self.status_bar.pack(side=tk.BOTTOM, fill=tk.X)
        
        # Handle window close
        self.win.protocol("WM_DELETE_WINDOW", self.on_close)
    
    def setup_processes_tab(self):
        # Control frame
        control_frame = tk.Frame(self.processes_frame)
        control_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # Buttons
        tk.Button(control_frame, text="Refresh", command=self.refresh_processes).pack(side=tk.LEFT, padx=5)
        tk.Button(control_frame, text="Close Program", command=self.close_selected_program).pack(side=tk.LEFT, padx=5)
        tk.Button(control_frame, text="Launch Program", command=self.launch_program_dialog).pack(side=tk.LEFT, padx=5)
        
        # Auto-refresh checkbox
        self.auto_refresh_var = tk.BooleanVar(value=True)
        tk.Checkbutton(control_frame, text="Auto Refresh", variable=self.auto_refresh_var,
                      command=self.toggle_auto_refresh).pack(side=tk.LEFT, padx=5)
        
        # Search frame
        search_frame = tk.Frame(self.processes_frame)
        search_frame.pack(fill=tk.X, padx=5, pady=5)
        
        tk.Label(search_frame, text="Search:").pack(side=tk.LEFT)
        self.search_var = tk.StringVar()
        self.search_var.trace_add("write", self.filter_processes)
        tk.Entry(search_frame, textvariable=self.search_var, width=30).pack(side=tk.LEFT, padx=5)
        
        # Process tree
        tree_frame = tk.Frame(self.processes_frame)
        tree_frame.pack(fill='both', expand=True, padx=5, pady=5)
        
        # Scrollbars
        v_scrollbar = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL)
        h_scrollbar = ttk.Scrollbar(tree_frame, orient=tk.HORIZONTAL)
        
        # Treeview
        self.tree = ttk.Treeview(
            tree_frame,
            columns=("Program", "Window", "Status", "PID"),
            show='headings',
            yscrollcommand=v_scrollbar.set,
            xscrollcommand=h_scrollbar.set
        )
        
        # Configure scrollbars
        v_scrollbar.config(command=self.tree.yview)
        h_scrollbar.config(command=self.tree.xview)
        
        # Column headings
        self.tree.heading("Program", text="Program Name", command=lambda: self.sort_column("Program"))
        self.tree.heading("Window", text="Window Title", command=lambda: self.sort_column("Window"))
        self.tree.heading("Status", text="Status", command=lambda: self.sort_column("Status"))
        self.tree.heading("PID", text="PID", command=lambda: self.sort_column("PID"))
        
        # Column widths
        self.tree.column("Program", width=200, anchor=tk.W)
        self.tree.column("Window", width=250, anchor=tk.W)
        self.tree.column("Status", width=100, anchor=tk.CENTER)
        self.tree.column("PID", width=80, anchor=tk.CENTER)
        
        # Pack treeview and scrollbars
        self.tree.grid(row=0, column=0, sticky='nsew')
        v_scrollbar.grid(row=0, column=1, sticky='ns')
        h_scrollbar.grid(row=1, column=0, sticky='ew')
        
        # Configure grid weights
        tree_frame.grid_rowconfigure(0, weight=1)
        tree_frame.grid_columnconfigure(0, weight=1)
        
        # Bind events
        self.tree.bind("<Double-1>", self.show_program_details)
        self.tree.bind("<Button-3>", self.show_context_menu)
        
        # Store all processes for filtering
        self.all_processes = []
        self.sort_reverse = False
        self.sort_column_name = "Program"
    
    def setup_performance_tab(self):
        # System info
        info_frame = tk.LabelFrame(self.performance_frame, text="Desktop Environment Information")
        info_frame.pack(fill=tk.X, padx=5, pady=5)
        
        self.programs_count_label = tk.Label(info_frame, text="Running Desktop Programs: 0")
        self.programs_count_label.pack(anchor=tk.W, padx=5, pady=2)
        
        self.windows_count_label = tk.Label(info_frame, text="Open Windows: 0")
        self.windows_count_label.pack(anchor=tk.W, padx=5, pady=2)
        
        self.cpu_label = tk.Label(info_frame, text="System CPU Usage: 0%")
        self.cpu_label.pack(anchor=tk.W, padx=5, pady=2)
        
        self.memory_label = tk.Label(info_frame, text="System Memory Usage: 0%")
        self.memory_label.pack(anchor=tk.W, padx=5, pady=2)
        
        # Available programs list
        programs_frame = tk.LabelFrame(self.performance_frame, text="Available Desktop Programs")
        programs_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Create a scrollable list of available programs
        list_frame = tk.Frame(programs_frame)
        list_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        scrollbar_list = tk.Scrollbar(list_frame)
        scrollbar_list.pack(side=tk.RIGHT, fill=tk.Y)
        
        self.programs_listbox = tk.Listbox(list_frame, yscrollcommand=scrollbar_list.set)
        self.programs_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar_list.config(command=self.programs_listbox.yview)
        
        # Populate with available programs
        for program_name in sorted(self.desktop_programs.keys()):
            self.programs_listbox.insert(tk.END, program_name)
    
    def is_desktop_environment_process(self, proc_info):
        """Check if a process belongs to our desktop environment"""
        try:
            cmdline = proc_info.get('cmdline', [])
            if not cmdline:
                return False, None
            
            # Check if it's a Python process running one of our scripts
            if any('python' in str(arg).lower() for arg in cmdline):
                for cmd_arg in cmdline:
                    cmd_arg_str = str(cmd_arg)
                    # Check if any command line argument matches our script files
                    for program_name, script_file in self.desktop_programs.items():
                        if script_file in cmd_arg_str or cmd_arg_str.endswith(script_file):
                            return True, program_name
                    
                    # Also check if it's running from our script directory
                    if self.script_dir in cmd_arg_str:
                        # Try to extract program name from script path
                        script_name = os.path.basename(cmd_arg_str)
                        for program_name, script_file in self.desktop_programs.items():
                            if script_name == script_file:
                                return True, program_name
            
            return False, None
        except Exception:
            return False, None
    
    def refresh_processes(self):
        """Refresh the process list to show only desktop environment programs"""
        if not hasattr(self, 'win') or not self.win.winfo_exists():
            return
            
        try:
            # Clear existing items
            for item in self.tree.get_children():
                self.tree.delete(item)
            
            self.all_processes = []
            desktop_programs_count = 0
            seen_programs = set()
            
            # Check open windows from desktop environment
            if self.open_windows:
                for window in list(self.open_windows.keys()):
                    try:
                        if hasattr(window, 'title') and hasattr(window, 'winfo_exists'):
                            if window.winfo_exists():
                                window_title = window.title()
                                
                                # Try to match window title to program name
                                program_name = "Unknown Desktop Program"
                                for prog_name in self.desktop_programs.keys():
                                    if prog_name.lower() in window_title.lower():
                                        program_name = prog_name
                                        break
                                
                                # Avoid duplicates
                                program_key = (program_name, window_title)
                                if program_key not in seen_programs:
                                    seen_programs.add(program_key)
                                    
                                    process_data = {
                                        'program': program_name,
                                        'window': window_title,
                                        'status': 'Running',
                                        'pid': 'N/A',
                                        'window_obj': window
                                    }
                                    self.all_processes.append(process_data)
                                    desktop_programs_count += 1
                    except Exception:
                        continue
            
            # Also check system processes that might be our desktop programs
            for proc in psutil.process_iter(['pid', 'name', 'cmdline', 'status']):
                try:
                    pinfo = proc.info
                    is_desktop_prog, program_name = self.is_desktop_environment_process(pinfo)
                    
                    if is_desktop_prog:
                        # Create a key to check for duplicates
                        program_key = (program_name, pinfo['pid'])
                        
                        # Check if we haven't already added this program
                        if program_key not in seen_programs:
                            seen_programs.add(program_key)
                            
                            process_data = {
                                'program': program_name,
                                'window': f"Process: {pinfo['name']}",
                                'status': pinfo.get('status', 'Unknown'),
                                'pid': pinfo['pid'],
                                'window_obj': None
                            }
                            self.all_processes.append(process_data)
                            desktop_programs_count += 1
                            
                except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                    continue
            
            # Apply current filter
            self.filter_processes()
            
            # Update system info
            self.update_system_info(desktop_programs_count)
            
            self.status_bar.config(text=f"Last updated: {time.strftime('%H:%M:%S')} - {desktop_programs_count} desktop programs running")
            
        except Exception as e:
            self.status_bar.config(text=f"Error refreshing processes: {str(e)}")
    
    def filter_processes(self, *args):
        """Filter processes based on search term"""
        search_term = self.search_var.get().lower()
        
        # Clear tree
        for item in self.tree.get_children():
            self.tree.delete(item)
        
        # Filter and add processes
        for proc in self.all_processes:
            if not search_term or search_term in proc['program'].lower() or search_term in proc['window'].lower():
                self.tree.insert('', 'end', values=(
                    proc['program'],
                    proc['window'],
                    proc['status'],
                    proc['pid']
                ))
    
    def sort_column(self, column):
        """Sort processes by column"""
        if self.sort_column_name == column:
            self.sort_reverse = not self.sort_reverse
        else:
            self.sort_reverse = False
            self.sort_column_name = column
        
        # Sort the data
        if column == "Program":
            self.all_processes.sort(key=lambda x: x['program'].lower(), reverse=self.sort_reverse)
        elif column == "Window":
            self.all_processes.sort(key=lambda x: x['window'].lower(), reverse=self.sort_reverse)
        elif column == "Status":
            self.all_processes.sort(key=lambda x: x['status'], reverse=self.sort_reverse)
        elif column == "PID":
            self.all_processes.sort(key=lambda x: str(x['pid']), reverse=self.sort_reverse)
        
        # Refresh the display
        self.filter_processes()
    
    def update_system_info(self, programs_count):
        """Update system performance information"""
        try:
            # Desktop programs count
            self.programs_count_label.config(text=f"Running Desktop Programs: {programs_count}")
            
            # Open windows count
            windows_count = len(self.open_windows) if self.open_windows else 0
            self.windows_count_label.config(text=f"Open Windows: {windows_count}")
            
            # System CPU usage
            cpu_percent = psutil.cpu_percent(interval=None)
            self.cpu_label.config(text=f"System CPU Usage: {cpu_percent}%")
            
            # System Memory usage
            memory = psutil.virtual_memory()
            memory_percent = memory.percent
            self.memory_label.config(text=f"System Memory Usage: {memory_percent}%")
            
        except Exception as e:
            print(f"Error updating system info: {e}")
    
    def close_selected_program(self):
        """Close the selected desktop program"""
        selection = self.tree.selection()
        if not selection:
            messagebox.showwarning("No Selection", "Please select a program to close.")
            return
        
        item = selection[0]
        values = self.tree.item(item, 'values')
        program_name = values[0]
        window_title = values[1]
        pid = values[3]
        
        # Find the corresponding process in our list
        selected_process = None
        for proc in self.all_processes:
            if proc['program'] == program_name and proc['window'] == window_title:
                selected_process = proc
                break
        
        if not selected_process:
            messagebox.showerror("Error", "Could not find the selected program.")
            return
        
        # Confirm closure
        result = messagebox.askyesno(
            "Confirm Close",
            f"Are you sure you want to close '{program_name}'?\n\n"
            "This may cause data loss if the program has unsaved work."
        )
        
        if result:
            try:
                # If it's a window object, try to close it gracefully
                if selected_process['window_obj']:
                    try:
                        if selected_process['window_obj'].winfo_exists():
                            selected_process['window_obj'].destroy()
                            self.status_bar.config(text=f"Closed program: {program_name}")
                        else:
                            messagebox.showinfo("Info", "Program window no longer exists.")
                    except tk.TclError:
                        messagebox.showinfo("Info", "Program window no longer exists.")
                # If it's a process, try to terminate it
                elif pid != 'N/A':
                    proc = psutil.Process(int(pid))
                    proc.terminate()
                    # Wait a bit for graceful termination
                    try:
                        proc.wait(timeout=3)
                    except psutil.TimeoutExpired:
                        # Force kill if it doesn't terminate gracefully
                        proc.kill()
                    self.status_bar.config(text=f"Terminated process: {program_name} (PID: {pid})")
                else:
                    messagebox.showinfo("Info", "Cannot close this program - no process ID available.")
                    return
                
                # Refresh the list
                self.win.after(500, self.refresh_processes)
                
            except psutil.NoSuchProcess:
                messagebox.showinfo("Program Not Found", f"Program {program_name} no longer exists.")
                self.refresh_processes()
            except psutil.AccessDenied:
                messagebox.showerror("Access Denied", f"Cannot close program {program_name}. Access denied.")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to close program: {str(e)}")
    
    def launch_program_dialog(self):
        """Show dialog to launch a desktop program"""
        dialog = tk.Toplevel(self.win)
        dialog.title("Launch Desktop Program")
        dialog.geometry("350x400")
        dialog.resizable(False, False)
        dialog.grab_set()
        dialog.transient(self.win)
        
        tk.Label(dialog, text="Select a desktop program to launch:").pack(pady=10)
        
        # Create listbox with available programs
        list_frame = tk.Frame(dialog)
        list_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        
        scrollbar = tk.Scrollbar(list_frame)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        program_listbox = tk.Listbox(list_frame, yscrollcommand=scrollbar.set)
        program_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.config(command=program_listbox.yview)
        
        # Populate with available programs
        available_programs = list(self.desktop_programs.keys())
        for program_name in sorted(available_programs):
            program_listbox.insert(tk.END, program_name)
        
        button_frame = tk.Frame(dialog)
        button_frame.pack(pady=10)
        
        def launch_selected():
            selection = program_listbox.curselection()
            if not selection:
                messagebox.showwarning("No Selection", "Please select a program to launch.")
                return
            
            program_name = program_listbox.get(selection[0])
            
            try:
                # Use the program launcher if available
                if self.program_launcher and program_name in self.program_launcher:
                    launcher_func = self.program_launcher[program_name]
                    if callable(launcher_func):
                        launcher_func()
                        dialog.destroy()
                        self.status_bar.config(text=f"Launched: {program_name}")
                        # Refresh after a short delay
                        self.win.after(1000, self.refresh_processes)
                    else:
                        messagebox.showinfo("Info", f"Invalid launcher for {program_name}.")
                else:
                    # Try to launch by running the script file
                    script_file = self.desktop_programs.get(program_name)
                    if script_file:
                        script_path = os.path.join(self.script_dir, script_file)
                        if os.path.exists(script_path):
                            subprocess.Popen([sys.executable, script_path])
                            dialog.destroy()
                            self.status_bar.config(text=f"Launched: {program_name}")
                            self.win.after(1000, self.refresh_processes)
                        else:
                            messagebox.showerror("Error", f"Script file not found: {script_file}")
                    else:
                        messagebox.showinfo("Info", f"Cannot launch {program_name} - launcher not available.")
                        
            except Exception as e:
                messagebox.showerror("Error", f"Failed to launch '{program_name}': {str(e)}")
        
        tk.Button(button_frame, text="Launch", command=launch_selected).pack(side=tk.LEFT, padx=5)
        tk.Button(button_frame, text="Cancel", command=dialog.destroy).pack(side=tk.LEFT, padx=5)
    
    def show_program_details(self, event):
        """Show detailed information about the selected program"""
        selection = self.tree.selection()
        if not selection:
            return
        
        item = selection[0]
        values = self.tree.item(item, 'values')
        program_name = values[0]
        window_title = values[1]
        status = values[2]
        pid = values[3]
        
        # Find the corresponding process in our list
        selected_process = None
        for proc in self.all_processes:
            if proc['program'] == program_name and proc['window'] == window_title:
                selected_process = proc
                break
        
        # Create details window
        details_win = tk.Toplevel(self.win)
        details_win.title(f"Program Details - {program_name}")
        details_win.geometry("500x400")
        details_win.resizable(True, True)
        
        # Create text widget with scrollbar
        text_frame = tk.Frame(details_win)
        text_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        text_widget = tk.Text(text_frame, wrap=tk.WORD)
        scrollbar = ttk.Scrollbar(text_frame, orient=tk.VERTICAL, command=text_widget.yview)
        text_widget.configure(yscrollcommand=scrollbar.set)
        
        # Get detailed information
        details = []
        details.append(f"Program Name: {program_name}")
        details.append(f"Window Title: {window_title}")
        details.append(f"Status: {status}")
        details.append(f"Process ID: {pid}")
        
        if selected_process:
            details.append(f"Program Type: Desktop Environment Application")
            
            if program_name in self.desktop_programs:
                details.append(f"Script File: {self.desktop_programs[program_name]}")
            
            # If it's a system process, get more details
            if pid != 'N/A':
                try:
                    proc = psutil.Process(int(pid))
                    details.append(f"Process Name: {proc.name()}")
                    details.append(f"Create Time: {time.ctime(proc.create_time())}")
                    
                    cmdline = proc.cmdline()
                    if cmdline:
                        details.append(f"Command Line: {' '.join(cmdline)}")
                    
                    try:
                        memory_info = proc.memory_info()
                        details.append(f"Memory Usage: {round(memory_info.rss / 1024 / 1024, 1)} MB")
                    except:
                        details.append("Memory Usage: N/A")
                    
                    try:
                        details.append(f"CPU Percent: {proc.cpu_percent()}%")
                    except:
                        details.append("CPU Percent: N/A")
                        
                except psutil.NoSuchProcess:
                    details.append("Process no longer exists")
                except Exception as e:
                    details.append(f"Error getting process details: {e}")
        
        # Insert details into text widget
        text_widget.insert(tk.END, "\n".join(details))
        text_widget.config(state=tk.DISABLED)
        
        # Pack text widget and scrollbar
        text_widget.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
    
    def show_context_menu(self, event):
        """Show context menu for program"""
        selection = self.tree.selection()
        if not selection:
            return
        
        context_menu = tk.Menu(self.win, tearoff=0)
        context_menu.add_command(label="Close Program", command=self.close_selected_program)
        context_menu.add_command(label="Program Details", command=lambda: self.show_program_details(event))
        context_menu.add_separator()
        context_menu.add_command(label="Refresh", command=self.refresh_processes)
        
        try:
            context_menu.tk_popup(event.x_root, event.y_root)
        finally:
            context_menu.grab_release()
    
    def toggle_auto_refresh(self):
        """Toggle auto-refresh on/off"""
        self.auto_refresh = self.auto_refresh_var.get()
        if self.auto_refresh and not self._refresh_scheduled:
            self.start_auto_refresh()
    
    def start_auto_refresh(self):
        """Start automatic refresh"""
        if not hasattr(self, 'win') or not self.win.winfo_exists():
            self._refresh_scheduled = False
            return
            
        if self.auto_refresh:
            self.refresh_processes()
            self._refresh_scheduled = True
            self.win.after(self.refresh_interval, self.start_auto_refresh)
        else:
            self._refresh_scheduled = False
    
    def on_close(self):
        """Handle window close"""
        self.auto_refresh = False
        self._refresh_scheduled = False
        
        # Clean up any remaining references
        if hasattr(self, 'win'):
            try:
                self.win.destroy()
            except:
                pass


# Function to maintain compatibility with your existing code
def open_task_manager(root, program_launcher=None, open_windows=None):
    """Create and return a task manager window"""
    task_manager = TaskManager(root, program_launcher, open_windows)
    return task_manager.win


# Additional helper functions for desktop environment
def get_desktop_programs():
    """Get a list of running desktop environment programs"""
    # This would be called by the main desktop environment
    pass


def close_desktop_program(program_name):
    """Close a desktop environment program by name"""
    # This would be called by the main desktop environment
    pass