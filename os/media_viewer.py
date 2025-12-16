import customtkinter as ctk
import tkinter as tk
import os
from PIL import Image, ImageTk
import vlc
import sys

ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("blue")

media_path = "/home/yousuf-yasser-elshaer/codes/os/user files/media files"

def get_files(extension_list):
    return [f for f in os.listdir(media_path) if os.path.isfile(os.path.join(media_path, f)) and f.lower().endswith(tuple(extension_list))]

image_ext = ('.jpg', '.jpeg', '.png', '.gif', '.bmp')
video_ext = ('.mp4', '.avi', '.mkv', '.mov')
audio_ext = ('.mp3', '.wav', '.ogg', '.flac')

class MediaPlayer(ctk.CTkToplevel):
    def __init__(self, parent, path=None):
        super().__init__(parent)
        self.title("Media Player")
        self.geometry("800x600")
        
        self.instance = vlc.Instance()
        self.player = self.instance.media_player_new()

        self.video_panel = tk.Frame(self, bg="black")
        self.video_panel.pack(fill=ctk.BOTH, expand=1)

        self.progress_frame = ctk.CTkFrame(self)
        self.progress_frame.pack(fill=ctk.X, padx=10, pady=5)
        
        self.progress_slider = ctk.CTkSlider(
            self.progress_frame, from_=0, to=100,
            progress_color="blue", button_color="white"
        )
        self.progress_slider.pack(fill=ctk.X)
        self.progress_slider.bind("<Button-1>", self.on_slider_press)
        self.progress_slider.bind("<ButtonRelease-1>", self.on_slider_release)
        self.slider_dragging = False

        self.controls = ctk.CTkFrame(self)
        self.controls.pack(fill=ctk.X, padx=10, pady=5)

        self.open_button = ctk.CTkButton(self.controls, text="Open", command=self.open_file)
        self.open_button.pack(side=ctk.LEFT, padx=5)

        self.play_button = ctk.CTkButton(self.controls, text="Play", command=self.play_video)
        self.play_button.pack(side=ctk.LEFT, padx=5)

        self.pause_button = ctk.CTkButton(self.controls, text="Pause", command=self.pause_video)
        self.pause_button.pack(side=ctk.LEFT, padx=5)

        self.stop_button = ctk.CTkButton(self.controls, text="Stop", command=self.stop_video)
        self.stop_button.pack(side=ctk.LEFT, padx=5)

        self.volume_slider = ctk.CTkSlider(
            self.controls, from_=0, to=100,
            command=self.set_volume
        )
        self.volume_slider.set(50)
        self.volume_slider.pack(side=ctk.LEFT, padx=5)

        self.update_progress()

        self.protocol("WM_DELETE_WINDOW", self.on_close)

        if path:
            self.filename = path
            media = self.instance.media_new(self.filename)
            self.player.set_media(media)
            self.set_video_panel()
            self.play_video()

    def on_close(self):
        self.stop_video()
        self.destroy()

    def open_file(self):
        self.filename = tk.filedialog.askopenfilename(
            title="Select a Media File",
            filetypes=[("Media files", "*.mp4;*.avi;*.mkv;*.mov;*.mp3;*.wav;*.ogg;*.flac"), ("All files", "*.*")]
        )
        if self.filename:
            media = self.instance.media_new(self.filename)
            self.player.set_media(media)
            self.set_video_panel()
            self.play_video()

    def set_video_panel(self):
        if sys.platform.startswith("linux"):
            self.player.set_xwindow(self.video_panel.winfo_id())
        elif sys.platform == "win32":
            self.player.set_hwnd(self.video_panel.winfo_id())
        elif sys.platform == "darwin":
            self.player.set_nsobject(self.video_panel.winfo_id())

    def play_video(self):
        self.player.play()

    def pause_video(self):
        self.player.pause()

    def stop_video(self):
        self.player.stop()

    def set_volume(self, value):
        self.player.audio_set_volume(int(value))

    def on_slider_press(self, event):
        self.slider_dragging = True

    def on_slider_release(self, event):
        self.slider_dragging = False
        self.seek_video()

    def seek_video(self):
        slider_value = self.progress_slider.get()
        self.player.set_time(int(slider_value))

    def update_progress(self):
        current_time = self.player.get_time()
        duration = self.player.get_length()
        if duration > 0 and not self.slider_dragging:
            self.progress_slider.configure(to=duration)
            self.progress_slider.set(current_time)
        self.after(500, self.update_progress)

class MediaViewer:
    def __init__(self, root):
        self.root = root
        self.image_references = []  # Keep references to prevent garbage collection

        self.tabview = ctk.CTkTabview(self.root)
        self.tabview.pack(padx=20, pady=20, fill="both", expand=True)

        self.images_tab = self.tabview.add("Images")
        self.videos_tab = self.tabview.add("Videos")
        self.audios_tab = self.tabview.add("Audio")

        self.load_images()
        self.load_videos()
        self.load_audios()

    def load_images(self):
        self.images_scroll = ctk.CTkScrollableFrame(self.images_tab)
        self.images_scroll.pack(fill="both", expand=True)

        images = get_files(image_ext)
        cols = 3
        for i, img_file in enumerate(images):
            row = i // cols
            col = i % cols
            path = os.path.join(media_path, img_file)
            try:
                img = Image.open(path)
                img.thumbnail((200, 200), Image.Resampling.LANCZOS)
                photo = ImageTk.PhotoImage(img)
                
                # Create a frame for each image to make it clickable
                frame = ctk.CTkFrame(self.images_scroll)
                frame.grid(row=row, column=col, padx=10, pady=10)
                
                label = ctk.CTkLabel(frame, image=photo, text="")
                label.pack()
                
                # Store reference to prevent garbage collection
                self.image_references.append(photo)
                
                # Bind click event to both frame and label
                label.bind("<Button-1>", lambda e, p=path: self.view_image(p))
                frame.bind("<Button-1>", lambda e, p=path: self.view_image(p))
                
            except Exception as e:
                print(f"Error loading image {img_file}: {e}")

    def load_videos(self):
        self.videos_scroll = ctk.CTkScrollableFrame(self.videos_tab)
        self.videos_scroll.pack(fill="both", expand=True)

        videos = get_files(video_ext)
        for v in videos:
            path = os.path.join(media_path, v)
            btn = ctk.CTkButton(self.videos_scroll, text=v, command=lambda p=path: self.play_media(p))
            btn.pack(fill="x", pady=5)

    def load_audios(self):
        self.audios_scroll = ctk.CTkScrollableFrame(self.audios_tab)
        self.audios_scroll.pack(fill="both", expand=True)

        audios = get_files(audio_ext)
        for a in audios:
            path = os.path.join(media_path, a)
            btn = ctk.CTkButton(self.audios_scroll, text=a, command=lambda p=path: self.play_media(p))
            btn.pack(fill="x", pady=5)

    def view_image(self, path):
        top = ctk.CTkToplevel(self.root)
        top.title(os.path.basename(path))
        top.geometry("900x700")
        
        try:
            img = Image.open(path)
            max_size = 800
            if img.width > max_size or img.height > max_size:
                ratio = min(max_size / img.width, max_size / img.height)
                new_size = (int(img.width * ratio), int(img.height * ratio))
                img = img.resize(new_size, Image.Resampling.LANCZOS)
            
            photo = ImageTk.PhotoImage(img)
            label = ctk.CTkLabel(top, image=photo, text="")
            label.image = photo  # Keep a reference
            label.pack(expand=True, fill="both", padx=20, pady=20)
            
        except Exception as e:
            print(f"Error viewing image {path}: {e}")
            error_label = ctk.CTkLabel(top, text=f"Error loading image:\n{str(e)}", 
                                      font=("Arial", 14))
            error_label.pack(expand=True, padx=20, pady=20)

    def play_media(self, path):
        MediaPlayer(self.root, path)

def launch_media_viewer(parent, file_path=None, register=None, remove=None):
    root = ctk.CTkToplevel(parent)
    root.title("Media Viewer")
    root.geometry("800x600")
    app = MediaViewer(root)
    
    if register:
        register(root)
    
    def on_close():
        if remove:
            remove(root)
        root.destroy()
    
    root.protocol("WM_DELETE_WINDOW", on_close)
    
    if file_path:
        ext = os.path.splitext(file_path)[1].lower()
        if ext in image_ext:
            app.view_image(file_path)
        else:
            MediaPlayer(root, file_path)

    return root

if __name__ == "__main__":
    root = ctk.CTk()
    launch_media_viewer(root)
    root.mainloop()