#!/usr/bin/env python3
"""
YouOS 10 - Calculator Application
Standalone calculator that can be launched by the desktop manager
"""

import sys
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QPushButton, QGridLayout
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QFont

class CalculatorApp(QWidget):
    def __init__(self, is_standalone=True):
        super().__init__()
        self.is_standalone = is_standalone
        self.expression = "0"
        self.setup_ui()
    
    def setup_ui(self):
        self.setStyleSheet("""
            QWidget {
                background: #1a1a2e;
                color: white;
            }
            QLineEdit {
                background: #0f0f1e;
                border: 2px solid #3b82f6;
                border-radius: 8px;
                padding: 15px;
                font-size: 28px;
                font-weight: bold;
                color: white;
            }
            QPushButton {
                border: none;
                border-radius: 8px;
                font-size: 20px;
                font-weight: bold;
                min-height: 60px;
                margin: 2px;
            }
            QPushButton:hover {
                opacity: 0.8;
            }
        """)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(10)
        
        # Display
        self.display = QLineEdit()
        self.display.setText(self.expression)
        self.display.setAlignment(Qt.AlignmentFlag.AlignRight)
        self.display.setReadOnly(True)
        layout.addWidget(self.display)
        
        # Buttons
        grid = QGridLayout()
        grid.setSpacing(5)
        
        buttons = [
            ('C', 0, 0, '#ef4444'),
            ('⌫', 0, 1, '#f59e0b'),
            ('%', 0, 2, '#252538'),
            ('/', 0, 3, '#252538'),
            ('7', 1, 0, '#252538'),
            ('8', 1, 1, '#252538'),
            ('9', 1, 2, '#252538'),
            ('*', 1, 3, '#252538'),
            ('4', 2, 0, '#252538'),
            ('5', 2, 1, '#252538'),
            ('6', 2, 2, '#252538'),
            ('-', 2, 3, '#252538'),
            ('1', 3, 0, '#252538'),
            ('2', 3, 1, '#252538'),
            ('3', 3, 2, '#252538'),
            ('+', 3, 3, '#252538'),
            ('0', 4, 0, '#252538'),
            ('.', 4, 1, '#252538'),
            ('=', 4, 2, '#3b82f6'),
        ]
        
        for text, row, col, color in buttons:
            btn = QPushButton(text)
            if text == '=':
                btn.setStyleSheet(f"QPushButton {{ background: {color}; color: white; }}")
                grid.addWidget(btn, row, col, 1, 2)  # Span 2 columns
            elif text in ['C', '⌫']:
                btn.setStyleSheet(f"QPushButton {{ background: {color}; color: white; }}")
                grid.addWidget(btn, row, col)
            elif text in ['/', '*', '-', '+', '%']:
                btn.setStyleSheet(f"QPushButton {{ background: {color}; color: #3b82f6; }}")
                grid.addWidget(btn, row, col)
            else:
                btn.setStyleSheet(f"QPushButton {{ background: {color}; color: white; }}")
                grid.addWidget(btn, row, col)
            
            btn.clicked.connect(lambda checked, t=text: self.button_clicked(t))
        
        layout.addLayout(grid)
    
    def button_clicked(self, text):
        if text == 'C':
            self.clear()
        elif text == '⌫':
            self.backspace()
        elif text == '=':
            self.calculate()
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
            result = str(eval(self.expression))
            self.expression = result
            self.display.setText(result)
        except ZeroDivisionError:
            self.expression = "Error"
            self.display.setText("Error: Div by 0")
        except:
            self.expression = "Error"
            self.display.setText("Error")
    
    def clear(self):
        self.expression = "0"
        self.display.setText(self.expression)
    
    def backspace(self):
        if self.expression in ["Error", "Error: Div by 0"]:
            self.expression = "0"
        elif len(self.expression) > 1:
            self.expression = self.expression[:-1]
        else:
            self.expression = "0"
        self.display.setText(self.expression)


def main():
    from PyQt6.QtWidgets import QApplication
    app = QApplication(sys.argv)
    calculator = CalculatorApp()
    calculator.setWindowTitle("Calculator - YouOS 10")
    calculator.setFixedSize(350, 500)
    calculator.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()