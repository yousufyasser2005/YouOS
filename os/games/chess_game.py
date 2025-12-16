# chess_customtk.py
import customtkinter as ctk
import tkinter as tk
from PIL import Image, ImageTk, Image
import chess
import os
import random
import time
import threading

# Setup customtkinter theme
ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

# Path to pieces folder
PIECES_DIR = os.path.join(os.path.dirname(__file__), "pieces")

# ------------------ Chess AI ------------------
class ChessAI:
    def __init__(self, difficulty=2):
        self.difficulty = difficulty  # 1: Easy, 2: Medium, 3: Hard
        self.piece_values = {
            chess.PAWN: 100,
            chess.KNIGHT: 320,
            chess.BISHOP: 330,
            chess.ROOK: 500,
            chess.QUEEN: 900,
            chess.KING: 20000
        }
    
    def evaluate_board(self, board):
        if board.is_checkmate():
            if board.turn:
                return -10000
            else:
                return 10000
        
        if board.is_stalemate() or board.is_insufficient_material():
            return 0
        
        score = 0
        for square in chess.SQUARES:
            piece = board.piece_at(square)
            if piece:
                value = self.piece_values[piece.piece_type]
                if piece.color == chess.WHITE:
                    score += value
                else:
                    score -= value
        
        mobility = len(list(board.legal_moves))
        if board.turn == chess.WHITE:
            score += mobility * 2
        else:
            score -= mobility * 2
        
        return score
    
    def minimax(self, board, depth, alpha, beta, maximizing_player):
        if depth == 0 or board.is_game_over():
            return self.evaluate_board(board), None
        
        legal_moves = list(board.legal_moves)
        
        if maximizing_player:
            max_eval = float('-inf')
            best_move = None
            
            for move in legal_moves:
                board.push(move)
                eval, _ = self.minimax(board, depth-1, alpha, beta, False)
                board.pop()
                
                if eval > max_eval:
                    max_eval = eval
                    best_move = move
                
                alpha = max(alpha, eval)
                if beta <= alpha:
                    break
            
            return max_eval, best_move
        else:
            min_eval = float('inf')
            best_move = None
            
            for move in legal_moves:
                board.push(move)
                eval, _ = self.minimax(board, depth-1, alpha, beta, True)
                board.pop()
                
                if eval < min_eval:
                    min_eval = eval
                    best_move = move
                
                beta = min(beta, eval)
                if beta <= alpha:
                    break
            
            return min_eval, best_move
    
    def get_best_move(self, board):
        depth = self.difficulty + 1
        
        if self.difficulty == 1:
            return self.get_random_good_move(board)
        
        _, best_move = self.minimax(board, depth, float('-inf'), float('inf'), board.turn)
        
        if best_move is None and board.legal_moves.count() > 0:
            best_move = random.choice(list(board.legal_moves))
        
        return best_move
    
    def get_random_good_move(self, board):
        legal_moves = list(board.legal_moves)
        
        good_moves = []
        for move in legal_moves:
            if board.is_capture(move):
                good_moves.append(move)
        
        if good_moves:
            return random.choice(good_moves)
        
        center_moves = []
        center_squares = [chess.D4, chess.E4, chess.D5, chess.E5]
        for move in legal_moves:
            if move.to_square in center_squares:
                center_moves.append(move)
        
        if center_moves:
            return random.choice(center_moves)
        
        return random.choice(legal_moves) if legal_moves else None

# ------------------ GUI ------------------
class ChessGUI:
    def __init__(self, root, is_toplevel=True):
        self.root_parent = root
        # Use toplevel only if specified
        if is_toplevel:
            self.root = ctk.CTkToplevel(root)
        else:
            self.root = root
        self.root.title("Chess Game")
        self.root.geometry("640x750")
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        
        # Chess logic
        self.board = chess.Board()
        self.ai = ChessAI()
        self.play_vs_ai = False
        self.ai_thinking = False
        self.player_color = chess.WHITE
        
        # Canvas
        self.canvas = tk.Canvas(self.root, width=640, height=640, highlightthickness=0, bg="#2b2b2b")
        self.canvas.pack(padx=10, pady=(10, 5))
        
        # Control frame
        control_frame = ctk.CTkFrame(self.root, corner_radius=12, height=80)
        control_frame.pack(pady=5, fill="x", padx=10)
        control_frame.pack_propagate(False)
        
        # Left section - Status
        left_section = ctk.CTkFrame(control_frame, fg_color="transparent")
        left_section.pack(side=tk.LEFT, fill="y", padx=10)
        
        self.status_label = ctk.CTkLabel(left_section, text="White to move", 
                                         font=ctk.CTkFont(family="Arial", size=16, weight="bold"))
        self.status_label.pack(pady=10)
        
        # AI mode button
        self.ai_button = ctk.CTkButton(left_section, text="Play vs Computer", 
                                       command=self.toggle_ai_mode, width=150, height=35,
                                       font=ctk.CTkFont(family="Arial", size=12, weight="bold"),
                                       corner_radius=8)
        self.ai_button.pack()
        
        # Right section - AI Difficulty selector
        difficulty_frame = ctk.CTkFrame(control_frame, fg_color="transparent")
        difficulty_frame.pack(side=tk.RIGHT, padx=20, pady=10)
        
        ctk.CTkLabel(difficulty_frame, text="AI Difficulty:", 
                    font=ctk.CTkFont(family="Arial", size=13, weight="bold")).pack(pady=(0, 5))
        
        self.difficulty_menu = ctk.CTkOptionMenu(
            difficulty_frame,
            values=["Easy", "Medium", "Hard"],
            command=self.change_difficulty,
            width=120,
            height=32,
            font=ctk.CTkFont(family="Arial", size=13),
            dropdown_font=ctk.CTkFont(family="Arial", size=12),
            corner_radius=8
        )
        self.difficulty_menu.set("Medium")
        self.difficulty_menu.pack()
        
        # Interface variables
        self.selected_square = None
        self.images = {}
        self.legal_moves = []
        
        # Initialize
        self.load_piece_images()
        self.create_menu()
        self.root.update_idletasks()
        self.root.update()
        self.draw_board()
        self.canvas.update_idletasks()
        self.canvas.update()
        self.canvas.bind("<Button-1>", self.on_click)
        self.root.bind("<<CloseChessWindow>>", lambda e: None)

    def choose_color_window(self):
        win = ctk.CTkToplevel(self.root)
        win.title("Choose Color")
        win.geometry("350x250")
        win.resizable(False, False)
        win.transient(self.root)
        win.configure(fg_color=("#1f538d", "#1a1a1a"))
        
        # Center the window
        win.update_idletasks()
        x = (win.winfo_screenwidth() // 2) - (350 // 2)
        y = (win.winfo_screenheight() // 2) - (250 // 2)
        win.geometry(f"350x250+{x}+{y}")
        
        try:
            win.grab_set()
        except:
            pass

        ctk.CTkLabel(win, text="Choose your color to play vs computer", 
                     font=ctk.CTkFont(family="Arial", size=16, weight="bold")).pack(pady=20)

        btn_frame = ctk.CTkFrame(win, fg_color="transparent")
        btn_frame.pack(pady=10, padx=20, fill="x")

        ctk.CTkButton(btn_frame, text="Play as White", width=200, height=40,
                     command=lambda: self._choose_color_done(win, chess.WHITE),
                     font=ctk.CTkFont(family="Arial", size=13, weight="bold"),
                     corner_radius=10).pack(pady=10)

        ctk.CTkButton(btn_frame, text="Play as Black", width=200, height=40,
                     command=lambda: self._choose_color_done(win, chess.BLACK),
                     font=ctk.CTkFont(family="Arial", size=13, weight="bold"),
                     corner_radius=10).pack(pady=10)

        win.lift()
        win.focus_force()
        win.wait_window()

    def _choose_color_done(self, win, color):
        self.player_color = color
        self.ai_button.configure(text="Play vs Human")
        
        if self.player_color == chess.WHITE:
            self.status_label.configure(text="White to move (vs AI)")
            win.destroy()
            self.new_game()
        else:
            self.status_label.configure(text="Black to move (vs AI)")
            win.destroy()
            self.new_game()
            self.ai_thinking = True
            self.update_status()
            self.root.after(500, self.make_ai_move)

    def toggle_ai_mode(self):
        self.play_vs_ai = not self.play_vs_ai
        if self.play_vs_ai:
            self.choose_color_window()
        else:
            self.ai_button.configure(text="Play vs Computer")
            self.status_label.configure(text="White to move")
            self.player_color = chess.WHITE
            self.new_game()

    def change_difficulty(self, choice):
        difficulty_map = {"Easy": 1, "Medium": 2, "Hard": 3}
        self.ai.difficulty = difficulty_map.get(choice, 2)

    def create_menu(self):
        menubar = tk.Menu(self.root)
        game_menu = tk.Menu(menubar, tearoff=0)
        game_menu.add_command(label="New Game", command=self.new_game)
        game_menu.add_command(label="Undo Move", command=self.undo_move)
        game_menu.add_separator()
        game_menu.add_command(label="Play vs Computer", command=self.enable_ai_mode)
        game_menu.add_command(label="Play vs Human", command=self.disable_ai_mode)
        game_menu.add_separator()
        game_menu.add_command(label="Exit", command=self.on_close)
        menubar.add_cascade(label="Game", menu=game_menu)
        self.root.config(menu=menubar)

    def enable_ai_mode(self):
        self.play_vs_ai = True
        self.choose_color_window()

    def disable_ai_mode(self):
        self.play_vs_ai = False
        self.ai_button.configure(text="Play vs Computer")
        self.player_color = chess.WHITE
        self.new_game()

    def load_piece_images(self):
        pieces = ['r', 'n', 'b', 'q', 'k', 'p', 'R', 'N', 'B', 'Q', 'K', 'P']
        for piece in pieces:
            path = os.path.join(PIECES_DIR, f"{piece}.png")
            if os.path.exists(path):
                try:
                    img = Image.open(path).resize((80, 80))
                    self.images[piece] = ImageTk.PhotoImage(img)
                except:
                    self.create_default_image(piece)
            else:
                self.create_default_image(piece)

    def create_default_image(self, piece):
        try:
            img = Image.new('RGBA', (80, 80), (255, 255, 255, 0))
            self.images[piece] = ImageTk.PhotoImage(img)
        except:
            pass

    def draw_board(self):
        self.canvas.delete("all")
        colors = ["#f0d9b5", "#b58863"]
        
        # Draw squares
        for row in range(8):
            for col in range(8):
                x1 = col * 80
                y1 = row * 80
                square = chess.square(col, 7 - row)
                
                fill_color = colors[(row + col) % 2]
                if square == self.selected_square:
                    fill_color = "#9370DB"
                elif square in self.legal_moves:
                    fill_color = "#90EE90"
                elif self.board.is_check() and square == self.board.king(self.board.turn):
                    fill_color = "#FFB6C1"
                
                self.canvas.create_rectangle(x1, y1, x1+80, y1+80, 
                                           fill=fill_color, outline="black", width=2)
        
        # Unicode pieces
        piece_unicode = {
            'P': '♙', 'N': '♘', 'B': '♗', 'R': '♖', 'Q': '♕', 'K': '♔',
            'p': '♟', 'n': '♞', 'b': '♝', 'r': '♜', 'q': '♛', 'k': '♚'
        }
        
        # Draw pieces
        for row in range(8):
            for col in range(8):
                square = chess.square(col, 7 - row)
                piece = self.board.piece_at(square)
                if piece:
                    x1 = col * 80
                    y1 = row * 80
                    img = self.images.get(piece.symbol())
                    
                    if img and hasattr(img, 'width'):
                        self.canvas.create_image(x1 + 40, y1 + 40, image=img)
                    else:
                        symbol = piece_unicode.get(piece.symbol(), piece.symbol())
                        color = "white" if piece.color == chess.WHITE else "black"
                        self.canvas.create_text(x1 + 40, y1 + 40, 
                                              text=symbol, 
                                              font=("Arial", 48, "bold"),
                                              fill=color)
        
        self.update_status()

    def update_status(self):
        mode_text = " (vs AI)" if self.play_vs_ai else ""
        color = "White" if self.board.turn else "Black"
        status = f"{color} to move{mode_text}"
        
        if self.ai_thinking:
            status = "AI is thinking..."
        
        if self.board.is_check():
            status += " - CHECK!"
        if self.board.is_checkmate():
            status = "CHECKMATE! " + ("Black" if self.board.turn else "White") + " wins!"
        elif self.board.is_stalemate():
            status = "STALEMATE! Game is drawn."
        elif self.board.is_insufficient_material():
            status = "Draw by insufficient material!"
        
        self.status_label.configure(text=status)

    def on_click(self, event):
        if self.ai_thinking:
            return
            
        col = event.x // 80
        row = 7 - (event.y // 80)
        square = chess.square(col, row)

        if self.selected_square is None:
            piece = self.board.piece_at(square)
            if piece and piece.color == self.board.turn:
                if self.play_vs_ai and self.board.turn != self.player_color:
                    return
                self.selected_square = square
                self.legal_moves = [move.to_square for move in self.board.legal_moves 
                                   if move.from_square == square]
        else:
            move = self.create_move(self.selected_square, square)
            
            if move and move in self.board.legal_moves:
                self.make_move(move)
            else:
                self.selected_square = None
                self.legal_moves = []
                self.draw_board()

        self.draw_board()
        
        if self.play_vs_ai and not self.board.is_game_over() and self.board.turn != self.player_color:
            self.ai_thinking = True
            self.update_status()
            try:
                self.root.update()
            except:
                pass
            self.root.after(500, self.make_ai_move)

    def make_move(self, move):
        self.board.push(move)
        self.selected_square = None
        self.legal_moves = []
        
        if self.board.is_game_over():
            self.show_game_over()

    def make_ai_move(self):
        def _ai_job():
            try:
                ai_move = self.ai.get_best_move(self.board)
                if ai_move:
                    self.board.push(ai_move)
            except:
                legal_moves = list(self.board.legal_moves)
                if legal_moves:
                    self.board.push(random.choice(legal_moves))
            self.root.after(0, self._after_ai_move)

        threading.Thread(target=_ai_job, daemon=True).start()

    def _after_ai_move(self):
        self.ai_thinking = False
        self.draw_board()

    def create_move(self, from_square, to_square):
        piece = self.board.piece_at(from_square)
        if not piece:
            return None
        
        # Check if move is legal
        is_legal = False
        for legal_move in self.board.legal_moves:
            if legal_move.from_square == from_square and legal_move.to_square == to_square:
                is_legal = True
                break
        
        if not is_legal:
            return None
        
        # Check for pawn promotion
        promotion = None
        if piece.piece_type == chess.PAWN:
            to_rank = chess.square_rank(to_square)
            if (piece.color == chess.WHITE and to_rank == 7) or \
               (piece.color == chess.BLACK and to_rank == 0):
                promotion = self.get_promotion_choice()
                if not promotion:
                    return None
        
        return chess.Move(from_square, to_square, promotion=promotion)

    def get_promotion_choice(self):
        promo_window = ctk.CTkToplevel(self.root)
        promo_window.title("Pawn Promotion")
        promo_window.geometry("320x280")
        promo_window.transient(self.root)
        promo_window.resizable(False, False)
        promo_window.configure(fg_color=("#1f538d", "#1a1a1a"))
        
        # Center the window
        promo_window.update_idletasks()
        x = (promo_window.winfo_screenwidth() // 2) - (320 // 2)
        y = (promo_window.winfo_screenheight() // 2) - (280 // 2)
        promo_window.geometry(f"320x280+{x}+{y}")
        
        try:
            promo_window.grab_set()
        except:
            pass
        
        promo_window.lift()
        promo_window.focus_force()
        
        ctk.CTkLabel(promo_window, text="Choose piece for promotion:", 
                     font=ctk.CTkFont(family="Arial", size=15, weight="bold")).pack(pady=15)
        
        choice_var = tk.StringVar(value="q")
        frame = ctk.CTkFrame(promo_window, corner_radius=10)
        frame.pack(pady=10, padx=20, fill="x")
        
        options = [("Queen", "q"), ("Knight", "n"), ("Bishop", "b"), ("Rook", "r")]
        for text, val in options:
            ctk.CTkRadioButton(frame, text=text, variable=choice_var, value=val,
                              font=ctk.CTkFont(family="Arial", size=12),
                              radiobutton_width=20, radiobutton_height=20).pack(anchor='w', pady=8, padx=10)
        
        promo_window.result = None
        
        def confirm():
            promotion_map = {'q': chess.QUEEN, 'n': chess.KNIGHT, 
                           'b': chess.BISHOP, 'r': chess.ROOK}
            promo_window.result = promotion_map.get(choice_var.get(), chess.QUEEN)
            promo_window.destroy()
        
        ctk.CTkButton(promo_window, text="Confirm", command=confirm, width=120, height=35,
                     font=ctk.CTkFont(family="Arial", size=13, weight="bold"),
                     corner_radius=8).pack(pady=15)
        
        promo_window.update()
        promo_window.wait_window()
        
        return promo_window.result if promo_window.result else chess.QUEEN

    def show_game_over(self):
        if self.board.is_checkmate():
            winner = "Black" if self.board.turn else "White"
            message = f"Checkmate! {winner} wins!"
        elif self.board.is_stalemate():
            message = "Stalemate! Game is drawn."
        elif self.board.is_insufficient_material():
            message = "Draw by insufficient material!"
        else:
            message = "Game Over!"
        
        top = ctk.CTkToplevel(self.root)
        top.title("Game Over")
        top.geometry("400x250")
        top.resizable(False, False)
        top.transient(self.root)
        top.configure(fg_color=("#1f538d", "#1a1a1a"))
        
        # Center the window
        top.update_idletasks()
        x = (top.winfo_screenwidth() // 2) - (400 // 2)
        y = (top.winfo_screenheight() // 2) - (250 // 2)
        top.geometry(f"400x250+{x}+{y}")
        
        try:
            top.grab_set()
        except:
            pass
        
        # Message label
        ctk.CTkLabel(top, text=message, font=ctk.CTkFont(family="Arial", size=18, weight="bold"), 
                     text_color=("#ff4444", "#ff6666")).pack(pady=30)
        
        # Button frame
        button_frame = ctk.CTkFrame(top, fg_color="transparent")
        button_frame.pack(pady=20, expand=True)
        
        def play_again():
            top.destroy()
            self.new_game()
        
        def exit_game():
            top.destroy()
            self.on_close()
        
        ctk.CTkButton(button_frame, text="Play Again", width=140, height=45, command=play_again,
                     font=ctk.CTkFont(family="Arial", size=14, weight="bold"),
                     corner_radius=10, fg_color=("#2e7d32", "#1b5e20"),
                     hover_color=("#43a047", "#2e7d32")).pack(side="left", padx=10)
        
        ctk.CTkButton(button_frame, text="Exit", width=140, height=45, command=exit_game,
                     font=ctk.CTkFont(family="Arial", size=14, weight="bold"),
                     corner_radius=10, fg_color=("#d32f2f", "#b71c1c"),
                     hover_color=("#e53935", "#c62828")).pack(side="left", padx=10)
        
        # Force update to ensure everything is displayed
        top.update_idletasks()
        top.update()
        top.lift()
        top.focus_force()

    def new_game(self):
        self.board.reset()
        self.selected_square = None
        self.legal_moves = []
        self.ai_thinking = False
        self.update_status()
        self.draw_board()
        
        if self.play_vs_ai and self.player_color == chess.BLACK and not self.board.is_game_over():
            self.ai_thinking = True
            self.update_status()
            self.root.after(400, self.make_ai_move)

    def undo_move(self):
        if self.ai_thinking:
            return
            
        if len(self.board.move_stack) > 0:
            self.board.pop()
            if self.play_vs_ai and len(self.board.move_stack) > 0:
                self.board.pop()
                
            self.selected_square = None
            self.legal_moves = []
            self.ai_thinking = False
            self.update_status()
            self.draw_board()

    def on_close(self):
        try:
            self.root.event_generate("<<CloseChessWindow>>")
        except:
            pass
        try:
            self.root.destroy()
        except:
            pass

def open_chess_game(parent):
    return ChessGUI(parent).root

if __name__ == "__main__":
    app = ctk.CTk()
    game = ChessGUI(app, is_toplevel=False)
    app.mainloop()