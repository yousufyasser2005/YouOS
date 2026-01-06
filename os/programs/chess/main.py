"""
YouOS Chess Game
A beautiful chess game with AI opponent
"""

import sys
import random
import threading
from pathlib import Path
from PyQt6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout,
                              QLabel, QPushButton, QFrame, QDialog, QRadioButton,
                              QButtonGroup, QMessageBox)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QPoint, QObject
from PyQt6.QtGui import QPainter, QColor, QFont, QPen

try:
    import chess
except ImportError:
    print("Installing chess library...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "chess"])
    import chess

COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_hover': '#60a5fa',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
    'border': '#374151',
    'success': '#10b981',
    'error': '#ef4444',
    'warning': '#f59e0b',
}


class GlassFrame(QFrame):
    """Glassmorphic frame"""
    def __init__(self, parent=None, opacity=0.15):
        super().__init__(parent)
        self.setStyleSheet(f"""
            QFrame {{
                background: rgba(255, 255, 255, {opacity});
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 16px;
            }}
        """)


class ChessAI:
    """Chess AI with difficulty levels"""
    
    def __init__(self, difficulty=2):
        self.difficulty = difficulty
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
            return -10000 if board.turn else 10000
        if board.is_stalemate() or board.is_insufficient_material():
            return 0
        
        score = 0
        for square in chess.SQUARES:
            piece = board.piece_at(square)
            if piece:
                value = self.piece_values[piece.piece_type]
                score += value if piece.color == chess.WHITE else -value
        
        mobility = len(list(board.legal_moves))
        score += mobility * 2 if board.turn == chess.WHITE else -mobility * 2
        
        return score
    
    def minimax(self, board, depth, alpha, beta, maximizing):
        if depth == 0 or board.is_game_over():
            return self.evaluate_board(board), None
        
        legal_moves = list(board.legal_moves)
        best_move = None
        
        if maximizing:
            max_eval = float('-inf')
            for move in legal_moves:
                board.push(move)
                eval_score, _ = self.minimax(board, depth-1, alpha, beta, False)
                board.pop()
                
                if eval_score > max_eval:
                    max_eval = eval_score
                    best_move = move
                alpha = max(alpha, eval_score)
                if beta <= alpha:
                    break
            return max_eval, best_move
        else:
            min_eval = float('inf')
            for move in legal_moves:
                board.push(move)
                eval_score, _ = self.minimax(board, depth-1, alpha, beta, True)
                board.pop()
                
                if eval_score < min_eval:
                    min_eval = eval_score
                    best_move = move
                beta = min(beta, eval_score)
                if beta <= alpha:
                    break
            return min_eval, best_move
    
    def get_best_move(self, board):
        if self.difficulty == 1:
            return self.get_random_good_move(board)
        
        depth = self.difficulty + 1
        _, best_move = self.minimax(board, depth, float('-inf'), float('inf'), board.turn)
        
        if best_move is None:
            legal_moves = list(board.legal_moves)
            best_move = random.choice(legal_moves) if legal_moves else None
        
        return best_move
    
    def get_random_good_move(self, board):
        legal_moves = list(board.legal_moves)
        captures = [m for m in legal_moves if board.is_capture(m)]
        if captures:
            return random.choice(captures)
        
        center_squares = [chess.D4, chess.E4, chess.D5, chess.E5]
        center_moves = [m for m in legal_moves if m.to_square in center_squares]
        if center_moves:
            return random.choice(center_moves)
        
        return random.choice(legal_moves) if legal_moves else None


class ChessBoard(QWidget):
    """Chess board widget"""
    
    move_made = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(640, 640)
        self.board = chess.Board()
        self.selected_square = None
        self.legal_moves = []
        self.player_color = chess.WHITE
        self.ai_mode = False
        
        # Unicode chess pieces
        self.piece_unicode = {
            'P': '♙', 'N': '♘', 'B': '♗', 'R': '♖', 'Q': '♕', 'K': '♔',
            'p': '♟', 'n': '♞', 'b': '♝', 'r': '♜', 'q': '♛', 'k': '♚'
        }
    
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        square_size = 80
        light_color = QColor("#f0d9b5")
        dark_color = QColor("#b58863")
        
        # Draw board squares
        for row in range(8):
            for col in range(8):
                square = chess.square(col, 7 - row)
                x = col * square_size
                y = row * square_size
                
                # Determine square color
                if square == self.selected_square:
                    color = QColor("#9370DB")
                elif square in self.legal_moves:
                    color = QColor("#90EE90")
                elif self.board.is_check() and square == self.board.king(self.board.turn):
                    color = QColor("#FFB6C1")
                else:
                    color = light_color if (row + col) % 2 == 0 else dark_color
                
                painter.fillRect(x, y, square_size, square_size, color)
                painter.setPen(QPen(QColor("#000000"), 2))
                painter.drawRect(x, y, square_size, square_size)
        
        # Draw pieces
        for row in range(8):
            for col in range(8):
                square = chess.square(col, 7 - row)
                piece = self.board.piece_at(square)
                
                if piece:
                    x = col * square_size + square_size // 2
                    y = row * square_size + square_size // 2
                    
                    symbol = self.piece_unicode.get(piece.symbol(), piece.symbol())
                    color = QColor("white") if piece.color == chess.WHITE else QColor("black")
                    
                    painter.setPen(color)
                    painter.setFont(QFont("Arial", 48, QFont.Weight.Bold))
                    
                    # Draw text centered
                    text_rect = painter.fontMetrics().boundingRect(symbol)
                    painter.drawText(x - text_rect.width() // 2, 
                                   y + text_rect.height() // 3, symbol)
    
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            col = event.pos().x() // 80
            row = 7 - (event.pos().y() // 80)
            
            if 0 <= col < 8 and 0 <= row < 8:
                square = chess.square(col, row)
                self.handle_click(square)
    
    def handle_click(self, square):
        if self.selected_square is None:
            piece = self.board.piece_at(square)
            if piece and piece.color == self.board.turn:
                if self.ai_mode and self.board.turn != self.player_color:
                    return
                
                self.selected_square = square
                self.legal_moves = [m.to_square for m in self.board.legal_moves 
                                  if m.from_square == square]
        else:
            move = self.create_move(self.selected_square, square)
            if move and move in self.board.legal_moves:
                self.board.push(move)
                self.selected_square = None
                self.legal_moves = []
                self.move_made.emit()
            else:
                self.selected_square = None
                self.legal_moves = []
        
        self.update()
    
    def create_move(self, from_square, to_square):
        piece = self.board.piece_at(from_square)
        if not piece:
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
        dialog = QDialog(self)
        dialog.setWindowTitle("Pawn Promotion")
        dialog.setFixedSize(300, 250)
        dialog.setStyleSheet(f"""
            QDialog {{
                background: {COLORS['bg_secondary']};
                border: 1px solid {COLORS['border']};
                border-radius: 12px;
            }}
        """)
        
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(20, 20, 20, 20)
        
        title = QLabel("Choose promotion piece:")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 16px; font-weight: bold;")
        layout.addWidget(title)
        
        button_group = QButtonGroup(dialog)
        pieces = [
            ("Queen ♕", chess.QUEEN),
            ("Rook ♖", chess.ROOK),
            ("Bishop ♗", chess.BISHOP),
            ("Knight ♘", chess.KNIGHT)
        ]
        
        result = [chess.QUEEN]
        
        for text, piece_type in pieces:
            radio = QRadioButton(text)
            radio.setStyleSheet(f"""
                QRadioButton {{
                    color: {COLORS['text_primary']};
                    font-size: 14px;
                    padding: 8px;
                }}
            """)
            if piece_type == chess.QUEEN:
                radio.setChecked(True)
            button_group.addButton(radio)
            layout.addWidget(radio)
            radio.toggled.connect(lambda checked, p=piece_type: result.__setitem__(0, p) if checked else None)
        
        confirm_btn = QPushButton("Confirm")
        confirm_btn.setFixedHeight(40)
        confirm_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        confirm_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 14px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        confirm_btn.clicked.connect(dialog.accept)
        layout.addWidget(confirm_btn)
        
        dialog.exec()
        return result[0]
    
    def reset_board(self):
        self.board.reset()
        self.selected_square = None
        self.legal_moves = []
        self.update()
    
    def undo_move(self):
        if len(self.board.move_stack) > 0:
            self.board.pop()
            if self.ai_mode and len(self.board.move_stack) > 0:
                self.board.pop()
            self.selected_square = None
            self.legal_moves = []
            self.update()
            self.move_made.emit()


class AIWorker(QObject):
    """Worker for AI move calculation"""
    move_ready = pyqtSignal(object)
    
    def __init__(self, ai, board):
        super().__init__()
        self.ai = ai
        self.board = board
    
    def calculate_move(self):
        try:
            # Create a copy of the board for thread safety
            import copy
            board_copy = copy.deepcopy(self.board)
            move = self.ai.get_best_move(board_copy)
            self.move_ready.emit(move)
        except Exception as e:
            print(f"AI Error: {e}")
            self.move_ready.emit(None)


class ChessGame(QWidget):
    """Main chess game window"""
    
    def __init__(self):
        super().__init__()
        self.ai = ChessAI(difficulty=2)
        self.ai_thinking = False
        self.ai_worker = None
        self.setup_ui()
        self.setWindowTitle("♟️ Chess - YouOS")
        self.resize(700, 780)
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        # Header
        header = GlassFrame(opacity=0.1)
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(20, 15, 20, 15)
        
        title = QLabel("♟️ Chess Game")
        title.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-size: 24px;
            font-weight: bold;
        """)
        header_layout.addWidget(title)
        header_layout.addStretch()
        
        self.status_label = QLabel("White to move")
        self.status_label.setStyleSheet(f"""
            color: {COLORS['text_secondary']};
            font-size: 14px;
        """)
        header_layout.addWidget(self.status_label)
        
        layout.addWidget(header)
        
        # Chess board
        self.chess_board = ChessBoard()
        self.chess_board.move_made.connect(self.on_move_made)
        layout.addWidget(self.chess_board, alignment=Qt.AlignmentFlag.AlignCenter)
        
        # Controls
        controls = GlassFrame(opacity=0.1)
        controls_layout = QHBoxLayout(controls)
        controls_layout.setContentsMargins(20, 15, 20, 15)
        
        # Left side - game controls
        game_controls = QVBoxLayout()
        
        new_game_btn = QPushButton("🆕 New Game")
        new_game_btn.setFixedHeight(40)
        new_game_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        new_game_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['success']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 13px;
                font-weight: bold;
                padding: 0 20px;
            }}
            QPushButton:hover {{
                background: #059669;
            }}
        """)
        new_game_btn.clicked.connect(self.new_game)
        game_controls.addWidget(new_game_btn)
        
        undo_btn = QPushButton("↩️ Undo")
        undo_btn.setFixedHeight(40)
        undo_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        undo_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['bg_tertiary']};
                color: white;
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
                font-size: 13px;
                font-weight: bold;
                padding: 0 20px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_primary']};
            }}
        """)
        undo_btn.clicked.connect(self.undo_move)
        game_controls.addWidget(undo_btn)
        
        controls_layout.addLayout(game_controls)
        controls_layout.addStretch()
        
        # Right side - AI controls
        ai_controls = QVBoxLayout()
        
        self.ai_button = QPushButton("🤖 Play vs AI")
        self.ai_button.setFixedHeight(40)
        self.ai_button.setCursor(Qt.CursorShape.PointingHandCursor)
        self.ai_button.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 8px;
                font-size: 13px;
                font-weight: bold;
                padding: 0 20px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        self.ai_button.clicked.connect(self.toggle_ai_mode)
        ai_controls.addWidget(self.ai_button)
        
        # Difficulty selector
        difficulty_layout = QHBoxLayout()
        diff_label = QLabel("Difficulty:")
        diff_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        difficulty_layout.addWidget(diff_label)
        
        for i, (text, level) in enumerate([("Easy", 1), ("Medium", 2), ("Hard", 3)]):
            btn = QPushButton(text)
            btn.setFixedSize(60, 30)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setCheckable(True)
            if level == 2:
                btn.setChecked(True)
            btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['bg_tertiary']};
                    color: {COLORS['text_secondary']};
                    border: 1px solid {COLORS['border']};
                    border-radius: 6px;
                    font-size: 11px;
                }}
                QPushButton:checked {{
                    background: {COLORS['accent_primary']};
                    color: white;
                }}
                QPushButton:hover {{
                    border: 1px solid {COLORS['accent_primary']};
                }}
            """)
            btn.clicked.connect(lambda checked, l=level: self.set_difficulty(l))
            difficulty_layout.addWidget(btn)
        
        ai_controls.addLayout(difficulty_layout)
        controls_layout.addLayout(ai_controls)
        
        layout.addWidget(controls)
        
        # Apply dark theme
        self.setStyleSheet(f"""
            QWidget {{
                background: {COLORS['bg_primary']};
                color: {COLORS['text_primary']};
            }}
        """)
    
    def set_difficulty(self, level):
        self.ai.difficulty = level
        # Update button states
        buttons = self.findChildren(QPushButton)
        for btn in buttons:
            if btn.text() in ["Easy", "Medium", "Hard"]:
                btn.setChecked(False)
        
        sender = self.sender()
        if sender:
            sender.setChecked(True)
    
    def toggle_ai_mode(self):
        if not self.chess_board.ai_mode:
            # Ask for color choice
            dialog = QDialog(self)
            dialog.setWindowTitle("Choose Color")
            dialog.setFixedSize(300, 200)
            dialog.setStyleSheet(f"""
                QDialog {{
                    background: {COLORS['bg_secondary']};
                    border: 1px solid {COLORS['border']};
                    border-radius: 12px;
                }}
            """)
            
            layout = QVBoxLayout(dialog)
            layout.setContentsMargins(20, 20, 20, 20)
            
            title = QLabel("Choose your color:")
            title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 16px; font-weight: bold;")
            layout.addWidget(title)
            
            white_btn = QPushButton("⚪ Play as White")
            white_btn.setFixedHeight(40)
            white_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            white_btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['accent_primary']};
                    color: white;
                    border: none;
                    border-radius: 8px;
                    font-size: 14px;
                    font-weight: bold;
                }}
                QPushButton:hover {{
                    background: {COLORS['accent_hover']};
                }}
            """)
            white_btn.clicked.connect(lambda: self.start_ai_game(chess.WHITE, dialog))
            layout.addWidget(white_btn)
            
            black_btn = QPushButton("⚫ Play as Black")
            black_btn.setFixedHeight(40)
            black_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            black_btn.setStyleSheet(f"""
                QPushButton {{
                    background: {COLORS['bg_tertiary']};
                    color: white;
                    border: 1px solid {COLORS['border']};
                    border-radius: 8px;
                    font-size: 14px;
                    font-weight: bold;
                }}
                QPushButton:hover {{
                    background: {COLORS['accent_primary']};
                }}
            """)
            black_btn.clicked.connect(lambda: self.start_ai_game(chess.BLACK, dialog))
            layout.addWidget(black_btn)
            
            dialog.exec()
        else:
            self.chess_board.ai_mode = False
            self.ai_button.setText("🤖 Play vs AI")
            self.update_status()
    
    def start_ai_game(self, player_color, dialog):
        dialog.accept()
        self.chess_board.ai_mode = True
        self.chess_board.player_color = player_color
        self.ai_button.setText("👥 Play vs Human")
        self.new_game()
        
        if player_color == chess.BLACK:
            QTimer.singleShot(500, self.make_ai_move)
    
    def on_move_made(self):
        self.update_status()
        
        if self.chess_board.board.is_game_over():
            self.show_game_over()
        elif self.chess_board.ai_mode and self.chess_board.board.turn != self.chess_board.player_color:
            self.ai_thinking = True
            self.update_status()
            QTimer.singleShot(500, self.make_ai_move)
    
    def make_ai_move(self):
        """Make AI move in background thread"""
        self.ai_worker = AIWorker(self.ai, self.chess_board.board)
        self.ai_worker.move_ready.connect(self.apply_ai_move)
        
        # Run in thread
        threading.Thread(target=self.ai_worker.calculate_move, daemon=True).start()
    
    def apply_ai_move(self, move):
        """Apply AI move from main thread"""
        if move:
            self.chess_board.board.push(move)
        
        self.ai_thinking = False
        self.chess_board.update()
        self.update_status()
        
        if self.chess_board.board.is_game_over():
            self.show_game_over()
        
        self.ai_worker = None
    
    def update_status(self):
        if self.ai_thinking:
            self.status_label.setText("🤖 AI is thinking...")
            return
        
        color = "White" if self.chess_board.board.turn else "Black"
        mode = " (vs AI)" if self.chess_board.ai_mode else ""
        status = f"{color} to move{mode}"
        
        if self.chess_board.board.is_check():
            status += " - CHECK!"
        
        self.status_label.setText(status)
    
    def new_game(self):
        self.chess_board.reset_board()
        self.ai_thinking = False
        self.update_status()
        
        if self.chess_board.ai_mode and self.chess_board.player_color == chess.BLACK:
            QTimer.singleShot(500, self.make_ai_move)
    
    def undo_move(self):
        if not self.ai_thinking:
            self.chess_board.undo_move()
            self.update_status()
    
    def show_game_over(self):
        board = self.chess_board.board
        
        if board.is_checkmate():
            winner = "Black" if board.turn else "White"
            message = f"Checkmate! {winner} wins!"
            icon = "🏆"
        elif board.is_stalemate():
            message = "Stalemate! Game is drawn."
            icon = "🤝"
        elif board.is_insufficient_material():
            message = "Draw by insufficient material!"
            icon = "🤝"
        else:
            message = "Game Over!"
            icon = "🎮"
        
        msg = QMessageBox(self)
        msg.setWindowTitle("Game Over")
        msg.setText(f"{icon} {message}")
        msg.setInformativeText("Would you like to play again?")
        msg.setStandardButtons(QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        msg.setStyleSheet(f"""
            QMessageBox {{
                background: {COLORS['bg_secondary']};
            }}
            QLabel {{
                color: {COLORS['text_primary']};
                font-size: 14px;
            }}
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 6px;
                padding: 8px 20px;
                font-size: 13px;
            }}
            QPushButton:hover {{
                background: {COLORS['accent_hover']};
            }}
        """)
        
        if msg.exec() == QMessageBox.StandardButton.Yes:
            self.new_game()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = ChessGame()
    window.show()
    sys.exit(app.exec())
