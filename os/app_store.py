import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import os
import json
import requests
import zipfile
import shutil
import threading
from datetime import datetime
import tempfile
import re
from urllib.parse import urlparse, parse_qs

class AppStore:
    def __init__(self, root, on_close_callback=None):
        self.root = root
        self.on_close_callback = on_close_callback
        self.store_window = None
        self.programs_dir = os.path.join(os.path.dirname(__file__), "programs")
        self.store_data_file = os.path.join(os.path.dirname(__file__), "store_data.json")
        self.available_apps = []
        self.installed_apps = []
        self.downloading = False
        
        # Ensure programs directory exists
        if not os.path.exists(self.programs_dir):
            os.makedirs(self.programs_dir)
            
        self.create_store_window()
        self.load_store_data()
        self.refresh_installed_apps()
        
    def create_store_window(self):
        """Create the main app store window"""
        self.store_window = tk.Toplevel(self.root)
        self.store_window.title("YouOS App Store")
        self.store_window.geometry("900x700")
        self.store_window.configure(bg='#f0f0f0')
        
        # Handle window close
        self.store_window.protocol("WM_DELETE_WINDOW", self.close_store)
        
        # Create header
        self.create_header()
        
        # Create main content area
        self.create_main_content()
        
        # Create status bar
        self.create_status_bar()
        
    def create_header(self):
        """Create the store header with navigation"""
        header_frame = tk.Frame(self.store_window, bg='#0078d4', height=80)
        header_frame.pack(fill=tk.X)
        header_frame.pack_propagate(False)
        
        # Store title
        title_label = tk.Label(
            header_frame, text="📱 YouOS App Store", 
            font=('Arial', 20, 'bold'), fg='white', bg='#0078d4'
        )
        title_label.pack(side=tk.LEFT, padx=20, pady=20)
        
        # Navigation buttons
        nav_frame = tk.Frame(header_frame, bg='#0078d4')
        nav_frame.pack(side=tk.RIGHT, padx=20, pady=20)
        
        self.browse_btn = tk.Button(
            nav_frame, text="Browse Apps", command=self.show_browse_tab,
            bg='white', fg='#0078d4', font=('Arial', 10, 'bold'),
            padx=15, pady=5, relief=tk.FLAT, cursor='hand2'
        )
        self.browse_btn.pack(side=tk.LEFT, padx=5)
        
        self.installed_btn = tk.Button(
            nav_frame, text="Installed", command=self.show_installed_tab,
            bg='#005fa3', fg='white', font=('Arial', 10, 'bold'),
            padx=15, pady=5, relief=tk.FLAT, cursor='hand2'
        )
        self.installed_btn.pack(side=tk.LEFT, padx=5)
        
        self.add_app_btn = tk.Button(
            nav_frame, text="Add App", command=self.show_add_app_dialog,
            bg='#28a745', fg='white', font=('Arial', 10, 'bold'),
            padx=15, pady=5, relief=tk.FLAT, cursor='hand2'
        )
        self.add_app_btn.pack(side=tk.LEFT, padx=5)
        
    def create_main_content(self):
        """Create the main content area with tabs"""
        # Create notebook for tabs
        self.notebook = ttk.Notebook(self.store_window)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # Browse tab
        self.browse_frame = tk.Frame(self.notebook, bg='white')
        self.notebook.add(self.browse_frame, text="Browse Apps")
        
        # Installed tab
        self.installed_frame = tk.Frame(self.notebook, bg='white')
        self.notebook.add(self.installed_frame, text="Installed Apps")
        
        self.create_browse_tab()
        self.create_installed_tab()
        
    def create_browse_tab(self):
        """Create the browse apps tab"""
        # Search frame
        search_frame = tk.Frame(self.browse_frame, bg='white')
        search_frame.pack(fill=tk.X, padx=10, pady=10)
        
        tk.Label(search_frame, text="Search Apps:", bg='white', font=('Arial', 12)).pack(side=tk.LEFT)
        
        self.search_var = tk.StringVar()
        self.search_entry = tk.Entry(
            search_frame, textvariable=self.search_var, font=('Arial', 11),
            width=30
        )
        self.search_entry.pack(side=tk.LEFT, padx=10)
        self.search_entry.bind('<KeyRelease>', self.filter_apps)
        
        refresh_btn = tk.Button(
            search_frame, text="🔄 Refresh", command=self.refresh_available_apps,
            bg='#17a2b8', fg='white', font=('Arial', 10),
            padx=10, pady=5, relief=tk.FLAT, cursor='hand2'
        )
        refresh_btn.pack(side=tk.RIGHT, padx=10)
        
        # Apps list frame with scrollbar
        list_frame = tk.Frame(self.browse_frame, bg='white')
        list_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # Create canvas and scrollbar for apps list
        canvas = tk.Canvas(list_frame, bg='white')
        scrollbar = ttk.Scrollbar(list_frame, orient="vertical", command=canvas.yview)
        self.apps_scrollable_frame = tk.Frame(canvas, bg='white')
        
        self.apps_scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        
        canvas.create_window((0, 0), window=self.apps_scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        self.apps_canvas = canvas
        
    def create_installed_tab(self):
        """Create the installed apps tab"""
        # Header
        header = tk.Label(
            self.installed_frame, text="Installed Applications", 
            font=('Arial', 16, 'bold'), bg='white'
        )
        header.pack(pady=20)
        
        # Installed apps list frame with scrollbar
        list_frame = tk.Frame(self.installed_frame, bg='white')
        list_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # Create canvas and scrollbar for installed apps
        canvas = tk.Canvas(list_frame, bg='white')
        scrollbar = ttk.Scrollbar(list_frame, orient="vertical", command=canvas.yview)
        self.installed_scrollable_frame = tk.Frame(canvas, bg='white')
        
        self.installed_scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        
        canvas.create_window((0, 0), window=self.installed_scrollable_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        
        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        self.installed_canvas = canvas
        
    def create_status_bar(self):
        """Create status bar at bottom"""
        self.status_frame = tk.Frame(self.store_window, bg='#e9ecef', height=30)
        self.status_frame.pack(fill=tk.X, side=tk.BOTTOM)
        self.status_frame.pack_propagate(False)
        
        self.status_label = tk.Label(
            self.status_frame, text="Ready", bg='#e9ecef', 
            font=('Arial', 9), anchor=tk.W
        )
        self.status_label.pack(side=tk.LEFT, padx=10, pady=5)
        
        # Progress bar (initially hidden)
        self.progress_var = tk.DoubleVar()
        self.progress_bar = ttk.Progressbar(
            self.status_frame, variable=self.progress_var, 
            maximum=100, length=200
        )
        
    def load_store_data(self):
        """Load store data from JSON file"""
        try:
            if os.path.exists(self.store_data_file):
                with open(self.store_data_file, 'r') as f:
                    data = json.load(f)
                    self.available_apps = data.get('available_apps', [])
            else:
                # Create default store data
                self.available_apps = [
                    {
                        "name": "Sample Calculator",
                        "description": "A simple calculator application",
                        "version": "1.0",
                        "author": "YouOS Team",
                        "drive_url": "",
                        "icon": "calculator.png",
                        "category": "Utilities",
                        "size": "2.5 MB",
                        "rating": 4.5,
                        "downloads": 1250
                    },
                    {
                        "name": "Text Editor Pro",
                        "description": "Advanced text editor with syntax highlighting",
                        "version": "2.1",
                        "author": "DevTools Inc",
                        "drive_url": "",
                        "icon": "editor.png",
                        "category": "Development",
                        "size": "5.8 MB",
                        "rating": 4.8,
                        "downloads": 3420
                    }
                ]
                self.save_store_data()
        except Exception as e:
            print(f"Error loading store data: {e}")
            self.available_apps = []
            
    def save_store_data(self):
        """Save store data to JSON file"""
        try:
            data = {
                'available_apps': self.available_apps,
                'last_updated': datetime.now().isoformat()
            }
            with open(self.store_data_file, 'w') as f:
                json.dump(data, f, indent=4)
        except Exception as e:
            print(f"Error saving store data: {e}")
            
    def refresh_available_apps(self):
        """Refresh the available apps display"""
        self.update_status("Refreshing apps...")
        
        # Clear current apps display
        for widget in self.apps_scrollable_frame.winfo_children():
            widget.destroy()
            
        # Display apps
        for i, app in enumerate(self.available_apps):
            self.create_app_card(self.apps_scrollable_frame, app, i)
            
        self.update_status(f"Found {len(self.available_apps)} apps")
        
    def refresh_installed_apps(self):
        """Refresh the installed apps display"""
        self.installed_apps = []
        
        # Scan programs directory
        if os.path.exists(self.programs_dir):
            for item in os.listdir(self.programs_dir):
                item_path = os.path.join(self.programs_dir, item)
                if os.path.isdir(item_path):
                    # Check for app info file
                    info_file = os.path.join(item_path, "app_info.json")
                    if os.path.exists(info_file):
                        try:
                            with open(info_file, 'r') as f:
                                app_info = json.load(f)
                                app_info['folder_name'] = item
                                self.installed_apps.append(app_info)
                        except:
                            # Create basic info for apps without info file
                            self.installed_apps.append({
                                'name': item.replace('_', ' ').title(),
                                'folder_name': item,
                                'version': 'Unknown',
                                'description': 'Installed application',
                                'install_date': 'Unknown'
                            })
                    else:
                        # Create basic info for apps without info file
                        self.installed_apps.append({
                            'name': item.replace('_', ' ').title(),
                            'folder_name': item,
                            'version': 'Unknown',
                            'description': 'Installed application',
                            'install_date': 'Unknown'
                        })
        
        # Clear current display
        for widget in self.installed_scrollable_frame.winfo_children():
            widget.destroy()
            
        # Display installed apps
        if self.installed_apps:
            for i, app in enumerate(self.installed_apps):
                self.create_installed_app_card(self.installed_scrollable_frame, app, i)
        else:
            no_apps_label = tk.Label(
                self.installed_scrollable_frame, 
                text="No apps installed yet.\nVisit the Browse tab to install apps!",
                font=('Arial', 14), fg='#666666', bg='white'
            )
            no_apps_label.pack(expand=True, pady=50)
            
    def create_app_card(self, parent, app, index):
        """Create a card for an available app"""
        # Card frame
        card_frame = tk.Frame(parent, bg='white', relief=tk.RAISED, bd=1)
        card_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # Alternate background colors
        bg_color = '#f8f9fa' if index % 2 == 0 else 'white'
        card_frame.configure(bg=bg_color)
        
        # Main content frame
        content_frame = tk.Frame(card_frame, bg=bg_color)
        content_frame.pack(fill=tk.X, padx=15, pady=15)
        
        # Left side - App info
        info_frame = tk.Frame(content_frame, bg=bg_color)
        info_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        # App name
        name_label = tk.Label(
            info_frame, text=app['name'], font=('Arial', 14, 'bold'),
            bg=bg_color, fg='#212529'
        )
        name_label.pack(anchor=tk.W)
        
        # App description
        desc_label = tk.Label(
            info_frame, text=app['description'], font=('Arial', 10),
            bg=bg_color, fg='#6c757d', wraplength=400
        )
        desc_label.pack(anchor=tk.W, pady=(2, 5))
        
        # App details
        details_frame = tk.Frame(info_frame, bg=bg_color)
        details_frame.pack(anchor=tk.W, fill=tk.X)
        
        # Version, Author, Category
        details_text = f"v{app['version']} • {app['author']} • {app['category']}"
        details_label = tk.Label(
            details_frame, text=details_text, font=('Arial', 9),
            bg=bg_color, fg='#6c757d'
        )
        details_label.pack(side=tk.LEFT)
        
        # Size and downloads
        stats_text = f"{app['size']} • {app['downloads']} downloads"
        stats_label = tk.Label(
            details_frame, text=stats_text, font=('Arial', 9),
            bg=bg_color, fg='#6c757d'
        )
        stats_label.pack(side=tk.RIGHT)
        
        # Right side - Actions
        action_frame = tk.Frame(content_frame, bg=bg_color)
        action_frame.pack(side=tk.RIGHT, padx=(10, 0))
        
        # Rating
        rating_text = f"⭐ {app['rating']}"
        rating_label = tk.Label(
            action_frame, text=rating_text, font=('Arial', 10, 'bold'),
            bg=bg_color, fg='#ffc107'
        )
        rating_label.pack(pady=(0, 5))
        
        # Install button
        install_btn = tk.Button(
            action_frame, text="📥 Install", 
            command=lambda a=app: self.install_app(a),
            bg='#28a745', fg='white', font=('Arial', 10, 'bold'),
            padx=20, pady=8, relief=tk.FLAT, cursor='hand2'
        )
        install_btn.pack()
        
        # Check if already installed
        app_folder = app['name'].lower().replace(' ', '_')
        if os.path.exists(os.path.join(self.programs_dir, app_folder)):
            install_btn.configure(text="✅ Installed", state=tk.DISABLED, bg='#6c757d')
            
    def create_installed_app_card(self, parent, app, index):
        """Create a card for an installed app"""
        # Card frame
        card_frame = tk.Frame(parent, bg='white', relief=tk.RAISED, bd=1)
        card_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # Alternate background colors
        bg_color = '#f8f9fa' if index % 2 == 0 else 'white'
        card_frame.configure(bg=bg_color)
        
        # Main content frame
        content_frame = tk.Frame(card_frame, bg=bg_color)
        content_frame.pack(fill=tk.X, padx=15, pady=15)
        
        # Left side - App info
        info_frame = tk.Frame(content_frame, bg=bg_color)
        info_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        # App name
        name_label = tk.Label(
            info_frame, text=app['name'], font=('Arial', 14, 'bold'),
            bg=bg_color, fg='#212529'
        )
        name_label.pack(anchor=tk.W)
        
        # App description
        desc_label = tk.Label(
            info_frame, text=app.get('description', 'No description available'), 
            font=('Arial', 10), bg=bg_color, fg='#6c757d', wraplength=400
        )
        desc_label.pack(anchor=tk.W, pady=(2, 5))
        
        # App details
        details_text = f"Version: {app.get('version', 'Unknown')}"
        if 'install_date' in app:
            details_text += f" • Installed: {app['install_date']}"
            
        details_label = tk.Label(
            info_frame, text=details_text, font=('Arial', 9),
            bg=bg_color, fg='#6c757d'
        )
        details_label.pack(anchor=tk.W)
        
        # Right side - Actions
        action_frame = tk.Frame(content_frame, bg=bg_color)
        action_frame.pack(side=tk.RIGHT, padx=(10, 0))
        
        # Launch button
        launch_btn = tk.Button(
            action_frame, text="🚀 Launch", 
            command=lambda a=app: self.launch_app(a),
            bg='#007bff', fg='white', font=('Arial', 10, 'bold'),
            padx=15, pady=5, relief=tk.FLAT, cursor='hand2'
        )
        launch_btn.pack(pady=2)
        
        # Uninstall button
        uninstall_btn = tk.Button(
            action_frame, text="🗑️ Uninstall", 
            command=lambda a=app: self.uninstall_app(a),
            bg='#dc3545', fg='white', font=('Arial', 10, 'bold'),
            padx=15, pady=5, relief=tk.FLAT, cursor='hand2'
        )
        uninstall_btn.pack(pady=2)
        
    def show_add_app_dialog(self):
        """Show dialog to add a new app"""
        dialog = tk.Toplevel(self.store_window)
        dialog.title("Add New App")
        dialog.geometry("500x600")
        dialog.configure(bg='white')
        dialog.transient(self.store_window)
        dialog.grab_set()
        
        # Center the dialog
        dialog.geometry("+%d+%d" % (
            self.store_window.winfo_rootx() + 200,
            self.store_window.winfo_rooty() + 50
        ))
        
        # Header
        header_label = tk.Label(
            dialog, text="Add New App to Store", 
            font=('Arial', 16, 'bold'), bg='white'
        )
        header_label.pack(pady=20)
        
        # Form frame
        form_frame = tk.Frame(dialog, bg='white')
        form_frame.pack(fill=tk.BOTH, expand=True, padx=30, pady=10)
        
        # Form fields
        fields = {}
        
        # App Name
        tk.Label(form_frame, text="App Name:", bg='white', font=('Arial', 10, 'bold')).pack(anchor=tk.W, pady=(10, 2))
        fields['name'] = tk.Entry(form_frame, font=('Arial', 11), width=50)
        fields['name'].pack(fill=tk.X, pady=(0, 10))
        
        # Description
        tk.Label(form_frame, text="Description:", bg='white', font=('Arial', 10, 'bold')).pack(anchor=tk.W, pady=(0, 2))
        fields['description'] = tk.Text(form_frame, font=('Arial', 11), height=3, width=50)
        fields['description'].pack(fill=tk.X, pady=(0, 10))
        
        # Version
        tk.Label(form_frame, text="Version:", bg='white', font=('Arial', 10, 'bold')).pack(anchor=tk.W, pady=(0, 2))
        fields['version'] = tk.Entry(form_frame, font=('Arial', 11), width=50)
        fields['version'].pack(fill=tk.X, pady=(0, 10))
        
        # Author
        tk.Label(form_frame, text="Author:", bg='white', font=('Arial', 10, 'bold')).pack(anchor=tk.W, pady=(0, 2))
        fields['author'] = tk.Entry(form_frame, font=('Arial', 11), width=50)
        fields['author'].pack(fill=tk.X, pady=(0, 10))
        
        # Category
        tk.Label(form_frame, text="Category:", bg='white', font=('Arial', 10, 'bold')).pack(anchor=tk.W, pady=(0, 2))
        fields['category'] = ttk.Combobox(form_frame, font=('Arial', 11), width=47)
        fields['category']['values'] = ('Utilities', 'Games', 'Development', 'Graphics', 'Internet', 'Office', 'Other')
        fields['category'].pack(fill=tk.X, pady=(0, 10))
        
        # Google Drive URL
        tk.Label(form_frame, text="Google Drive Share URL:", bg='white', font=('Arial', 10, 'bold')).pack(anchor=tk.W, pady=(0, 2))
        fields['drive_url'] = tk.Entry(form_frame, font=('Arial', 11), width=50)
        fields['drive_url'].pack(fill=tk.X, pady=(0, 10))
        
        # Help text
        help_text = tk.Label(
            form_frame, 
            text="📝 Instructions:\n1. Upload your app folder as a ZIP file to Google Drive\n2. Right-click the file and select 'Get link'\n3. Make sure it's set to 'Anyone with the link can view'\n4. Paste the share URL above",
            bg='#f8f9fa', fg='#6c757d', font=('Arial', 9),
            justify=tk.LEFT, wraplength=400, relief=tk.SOLID, bd=1
        )
        help_text.pack(fill=tk.X, pady=10, padx=10, ipady=10)
        
        # Size
        tk.Label(form_frame, text="Size (e.g., 2.5 MB):", bg='white', font=('Arial', 10, 'bold')).pack(anchor=tk.W, pady=(10, 2))
        fields['size'] = tk.Entry(form_frame, font=('Arial', 11), width=50)
        fields['size'].pack(fill=tk.X, pady=(0, 10))
        
        # Buttons
        button_frame = tk.Frame(dialog, bg='white')
        button_frame.pack(fill=tk.X, padx=30, pady=20)
        
        def save_app():
            # Validate fields
            name = fields['name'].get().strip()
            description = fields['description'].get(1.0, tk.END).strip()
            version = fields['version'].get().strip()
            author = fields['author'].get().strip()
            category = fields['category'].get().strip()
            drive_url = fields['drive_url'].get().strip()
            size = fields['size'].get().strip()
            
            if not all([name, description, version, author, category, drive_url, size]):
                messagebox.showerror("Error", "Please fill in all fields!")
                return
                
            # Create new app entry
            new_app = {
                "name": name,
                "description": description,
                "version": version,
                "author": author,
                "drive_url": drive_url,
                "icon": "default.png",
                "category": category,
                "size": size,
                "rating": 0.0,
                "downloads": 0
            }
            
            # Add to available apps
            self.available_apps.append(new_app)
            self.save_store_data()
            self.refresh_available_apps()
            
            messagebox.showinfo("Success", f"App '{name}' added to store successfully!")
            dialog.destroy()
        
        save_btn = tk.Button(
            button_frame, text="💾 Add App", command=save_app,
            bg='#28a745', fg='white', font=('Arial', 11, 'bold'),
            padx=20, pady=8, relief=tk.FLAT, cursor='hand2'
        )
        save_btn.pack(side=tk.RIGHT, padx=5)
        
        cancel_btn = tk.Button(
            button_frame, text="❌ Cancel", command=dialog.destroy,
            bg='#6c757d', fg='white', font=('Arial', 11, 'bold'),
            padx=20, pady=8, relief=tk.FLAT, cursor='hand2'
        )
        cancel_btn.pack(side=tk.RIGHT, padx=5)
        
    def extract_google_drive_id(self, url):
        """Extract file ID from Google Drive URL"""
        # Handle different Google Drive URL formats
        patterns = [
            r'/file/d/([a-zA-Z0-9-_]+)',
            r'id=([a-zA-Z0-9-_]+)',
            r'/d/([a-zA-Z0-9-_]+)'
        ]
        
        for pattern in patterns:
            match = re.search(pattern, url)
            if match:
                return match.group(1)
        return None
        
    def install_app(self, app):
        """Install an app from Google Drive"""
        if self.downloading:
            messagebox.showwarning("Download in Progress", "Please wait for the current download to complete.")
            return
            
        if not app['drive_url']:
            messagebox.showerror("Error", "No download URL provided for this app.")
            return
            
        # Extract Google Drive file ID
        file_id = self.extract_google_drive_id(app['drive_url'])
        if not file_id:
            messagebox.showerror("Error", "Invalid Google Drive URL. Please check the URL format.")
            return
            
        # Create app folder name
        app_folder = app['name'].lower().replace(' ', '_').replace('-', '_')
        app_path = os.path.join(self.programs_dir, app_folder)
        
        # Check if already installed
        if os.path.exists(app_path):
            messagebox.showinfo("Already Installed", f"{app['name']} is already installed.")
            return
            
        # Start download in separate thread
        self.downloading = True
        self.show_progress_bar()
        self.update_status(f"Downloading {app['name']}...")
        
        download_thread = threading.Thread(
            target=self.download_and_install,
            args=(file_id, app, app_path),
            daemon=True
        )
        download_thread.start()
        
    def download_and_install(self, file_id, app, app_path):
        """Download and install app in background thread"""
        try:
            # Google Drive direct download URL
            download_url = f"https://drive.google.com/uc?export=download&id={file_id}"
            
            # Create temporary file
            with tempfile.NamedTemporaryFile(delete=False, suffix='.zip') as temp_file:
                temp_path = temp_file.name
                
            # Download file with progress
            response = requests.get(download_url, stream=True)
            response.raise_for_status()
            
            total_size = int(response.headers.get('content-length', 0))
            downloaded = 0
            
            with open(temp_path, 'wb') as f:
                for chunk in response.iter_content(chunk_size=8192):
                    if chunk:
                        f.write(chunk)
                        downloaded += len(chunk)
                        if total_size > 0:
                            progress = (downloaded / total_size) * 100
                            self.store_window.after(0, lambda p=progress: self.update_progress(p))
            
            # Extract ZIP file
            self.store_window.after(0, lambda: self.update_status(f"Installing {app['name']}..."))
            
            with zipfile.ZipFile(temp_path, 'r') as zip_ref:
                zip_ref.extractall(app_path)
                
            # Create app info file
            app_info = {
                'name': app['name'],
                'description': app['description'],
                'version': app['version'],
                'author': app['author'],
                'category': app['category'],
                'install_date': datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            }
            
            info_file_path = os.path.join(app_path, 'app_info.json')
            with open(info_file_path, 'w') as f:
                json.dump(app_info, f, indent=4)
                
            # Update download count
            app['downloads'] += 1
            self.save_store_data()
            
            # Clean up
            os.unlink(temp_path)
            
            # Update UI
            self.store_window.after(0, self.installation_complete, app)
            
        except Exception as e:
            error_msg = f"Failed to install {app['name']}: {str(e)}"
            self.store_window.after(0, lambda: self.installation_failed(error_msg))
            
    def installation_complete(self, app):
        """Handle successful installation"""
        self.downloading = False
        self.hide_progress_bar()
        self.update_status(f"{app['name']} installed successfully!")
        
        # Refresh displays
        self.refresh_available_apps()
        self.refresh_installed_apps()
        
        messagebox.showinfo("Installation Complete", f"{app['name']} has been installed successfully!")
        
    def installation_failed(self, error_msg):
        """Handle failed installation"""
        self.downloading = False
        self.hide_progress_bar()
        self.update_status("Installation failed")
        messagebox.showerror("Installation Failed", error_msg)
        
    def uninstall_app(self, app):
        """Uninstall an app"""
        result = messagebox.askyesno(
            "Confirm Uninstall", 
            f"Are you sure you want to uninstall {app['name']}?\n\nThis will permanently delete all app files."
        )
        
        if result:
            try:
                app_path = os.path.join(self.programs_dir, app['folder_name'])
                if os.path.exists(app_path):
                    shutil.rmtree(app_path)
                    self.update_status(f"{app['name']} uninstalled successfully")
                    self.refresh_installed_apps()
                    self.refresh_available_apps()
                    messagebox.showinfo("Uninstall Complete", f"{app['name']} has been uninstalled.")
                else:
                    messagebox.showerror("Error", "App folder not found.")
            except Exception as e:
                messagebox.showerror("Uninstall Failed", f"Failed to uninstall {app['name']}: {str(e)}")
                
    def launch_app(self, app):
        """Launch an installed app"""
        try:
            # Generate a custom event to tell the main desktop to launch this program
            program_name = app['name']
            
            # Send event to main window to launch the program
            self.root.event_generate("<<LaunchProgramEvent>>", data=program_name)
            
            self.update_status(f"Launched {app['name']}")
            
        except Exception as e:
            messagebox.showerror("Launch Failed", f"Failed to launch {app['name']}: {str(e)}")
            
    def filter_apps(self, event=None):
        """Filter apps based on search query"""
        query = self.search_var.get().lower()
        
        # Clear current display
        for widget in self.apps_scrollable_frame.winfo_children():
            widget.destroy()
            
        # Filter and display apps
        filtered_apps = []
        for app in self.available_apps:
            if (query in app['name'].lower() or 
                query in app['description'].lower() or 
                query in app['category'].lower() or
                query in app['author'].lower()):
                filtered_apps.append(app)
                
        for i, app in enumerate(filtered_apps):
            self.create_app_card(self.apps_scrollable_frame, app, i)
            
        if not filtered_apps and query:
            no_results_label = tk.Label(
                self.apps_scrollable_frame, 
                text=f"No apps found matching '{query}'",
                font=('Arial', 12), fg='#666666', bg='white'
            )
            no_results_label.pack(expand=True, pady=50)
            
    def show_browse_tab(self):
        """Show browse tab"""
        self.notebook.select(0)
        self.browse_btn.configure(bg='white', fg='#0078d4')
        self.installed_btn.configure(bg='#005fa3', fg='white')
        
    def show_installed_tab(self):
        """Show installed tab"""
        self.notebook.select(1)
        self.refresh_installed_apps()
        self.installed_btn.configure(bg='white', fg='#0078d4')
        self.browse_btn.configure(bg='#005fa3', fg='white')
        
    def show_progress_bar(self):
        """Show progress bar"""
        self.progress_bar.pack(side=tk.RIGHT, padx=10, pady=5)
        
    def hide_progress_bar(self):
        """Hide progress bar"""
        self.progress_bar.pack_forget()
        
    def update_progress(self, value):
        """Update progress bar value"""
        self.progress_var.set(value)
        
    def update_status(self, message):
        """Update status bar message"""
        self.status_label.configure(text=message)
        
    def close_store(self):
        """Close the store window"""
        if self.on_close_callback:
            self.on_close_callback(self.store_window)
        else:
            self.store_window.destroy()

def open_app_store(root, on_close_callback=None):
    """Function to open the app store (called by main desktop)"""
    store = AppStore(root, on_close_callback)
    return store.store_window

# For testing standalone
if __name__ == "__main__":
    root = tk.Tk()
    root.withdraw()  # Hide main window
    store = AppStore(root)
    root.mainloop()
