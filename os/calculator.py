#!/usr/bin/env python3
"""
YouOS 10 - Calculator Application v1.1.0
Updated calculator with new visual design and features
"""

import sys
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QPushButton, QGridLayout, QLabel
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QFont

class CalculatorApp(QWidget):
    def __init__(self, is_standalone=True):
        super().__init__()
        self.is_standalone = is_standalone
        self.expression = "0"
        self.history = []
        self.setup_ui()
    
    def setup_ui(self):
        self.setStyleSheet("""
            QWidget {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:1, 
                    stop:0 #1a1a2e, stop:1 #2d1b69);
                color: white;
            }
            QLineEdit {
                background: rgba(15, 15, 30, 0.8);
                border: 3px solid #60a5fa;
                border-radius: 12px;
                padding: 20px;
                font-size: 32px;
                font-weight: bold;
                color: #60a5fa;
            }
            QPushButton {
                border: none;
                border-radius: 12px;
                font-size: 22px;
                font-weight: bold;
                min-height: 65px;
                margin: 3px;
            }
            QPushButton:hover {
                transform: scale(1.05);
                box-shadow: 0 8px 16px rgba(0,0,0,0.3);
            }
            QLabel {
                color: #9ca3af;
                font-size: 12px;
                padding: 5px;
            }
        """)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(15, 15, 15, 15)
        layout.setSpacing(15)
        
        # Title
        title = QLabel("YouOS Calculator v1.1.0")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setStyleSheet("font-size: 14px; font-weight: bold; color: #60a5fa;")
        layout.addWidget(title)
        
        # Display
        self.display = QLineEdit()
        self.display.setText(self.expression)
        self.display.setAlignment(Qt.AlignmentFlag.AlignRight)
        self.display.setReadOnly(True)
        layout.addWidget(self.display)
        
        # History display
        self.history_label = QLabel("History: Empty")
        self.history_label.setStyleSheet("color: #6b7280; font-size: 11px;")
        layout.addWidget(self.history_label)
        
        # Buttons with new colors
        grid = QGridLayout()
        grid.setSpacing(8)
        
        buttons = [
            ('AC', 0, 0, '#dc2626'),  # Red for clear all
            ('C', 0, 1, '#f59e0b'),   # Orange for clear
            ('⌫', 0, 2, '#f59e0b'),   # Orange for backspace
            ('/', 0, 3, '#8b5cf6'),   # Purple for operators
            ('7', 1, 0, '#374151'),
            ('8', 1, 1, '#374151'),
            ('9', 1, 2, '#374151'),
            ('*', 1, 3, '#8b5cf6'),
            ('4', 2, 0, '#374151'),
            ('5', 2, 1, '#374151'),
            ('6', 2, 2, '#374151'),
            ('-', 2, 3, '#8b5cf6'),
            ('1', 3, 0, '#374151'),
            ('2', 3, 1, '#374151'),
            ('3', 3, 2, '#374151'),
            ('+', 3, 3, '#8b5cf6'),
            ('±', 4, 0, '#6b7280'),   # Gray for sign change
            ('0', 4, 1, '#374151'),
            ('.', 4, 2, '#374151'),
            ('=', 4, 3, '#10b981'),   # Green for equals
        ]
        
        for text, row, col, color in buttons:
            btn = QPushButton(text)
            btn.setStyleSheet(f"QPushButton {{ background: {color}; color: white; }}")
            grid.addWidget(btn, row, col)
            btn.clicked.connect(lambda checked, t=text: self.button_clicked(t))
        
        layout.addLayout(grid)
        
        # Status bar
        status = QLabel("Enhanced Calculator with History • YouOS v1.1.0")
        status.setAlignment(Qt.AlignmentFlag.AlignCenter)
        status.setStyleSheet("color: #4b5563; font-size: 10px; padding: 8px;")
        layout.addWidget(status)
    
    def button_clicked(self, text):
        if text == 'AC':
            self.clear_all()
        elif text == 'C':
            self.clear()
        elif text == '⌫':
            self.backspace()
        elif text == '=':
            self.calculate()
        elif text == '±':
            self.toggle_sign()
        else:
            self.press(text)
    
    def press(self, char):
        if self.expression == "0" and char not in ['+', '-', '*', '/', '%', '.']:
            self.expression = str(char)
        elif self.expression == "Error":
            self.expression = str(char)
        else:
            self.expression += str(char)
        self.display.setText(self.expression)
    
    def calculate(self):
        try:
            original_expr = self.expression
            result = str(eval(self.expression))
            
            # Add to history
            self.history.append(f"{original_expr} = {result}")
            if len(self.history) > 3:
                self.history.pop(0)
            
            self.update_history_display()
            
            self.expression = result
            self.display.setText(result)
        except ZeroDivisionError:
            self.expression = "Error"
            self.display.setText("Error: Division by zero")
        except:
            self.expression = "Error"
            self.display.setText("Syntax Error")
    
    def clear(self):
        self.expression = "0"
        self.display.setText(self.expression)
    
    def clear_all(self):
        self.expression = "0"
        self.history = []
        self.display.setText(self.expression)
        self.update_history_display()
    
    def backspace(self):
        if self.expression in ["Error", "Error: Division by zero", "Syntax Error"]:
            self.expression = "0"
        elif len(self.expression) > 1:
            self.expression = self.expression[:-1]
        else:
            self.expression = "0"
        self.display.setText(self.expression)
    
    def toggle_sign(self):
        if self.expression != "0" and self.expression != "Error":
            if self.expression.startswith('-'):
                self.expression = self.expression[1:]
            else:
                self.expression = '-' + self.expression
            self.display.setText(self.expression)
    
    def update_history_display(self):
        if self.history:
            recent = self.history[-1] if len(self.history) == 1 else f"{self.history[-2]} | {self.history[-1]}"
            self.history_label.setText(f"History: {recent}")
        else:
            self.history_label.setText("History: Empty")


def main():
    from PyQt6.QtWidgets import QApplication
    app = QApplication(sys.argv)
    calculator = CalculatorApp()
    calculator.setWindowTitle("Calculator v1.1.0 - YouOS 10")
    calculator.setFixedSize(380, 600)
    calculator.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
