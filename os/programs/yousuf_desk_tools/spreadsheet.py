"""
Yousuf Desk Tools - Spreadsheet Module
spreadsheet.py - Full-featured spreadsheet application with formulas
"""

import re
import json
from pathlib import Path
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QTableWidget, 
                              QPushButton, QLabel, QLineEdit, QComboBox,
                              QFileDialog, QMessageBox, QFrame, QHeaderView,
                              QTableWidgetItem, QMenu)
from PyQt6.QtCore import Qt, pyqtSignal, QTimer
from PyQt6.QtGui import QFont, QColor, QBrush, QAction

try:
    from widgets import GlassFrame
    from utils import play_sound
except ImportError:
    def play_sound(name):
        pass
    class GlassFrame(QFrame):
        def __init__(self, parent=None, opacity=0.15):
            super().__init__(parent)

COLORS = {
    'bg_primary': '#0f0f1e',
    'bg_secondary': '#1a1a2e',
    'bg_tertiary': '#252538',
    'accent_primary': '#3b82f6',
    'accent_sheet': '#059669',
    'text_primary': '#ffffff',
    'text_secondary': '#9ca3af',
}


class FormulaEngine:
    """Simple formula engine for spreadsheet calculations"""
    
    @staticmethod
    def evaluate(formula, data):
        """Evaluate a formula with cell references"""
        if not formula.startswith('='):
            return formula
        
        formula = formula[1:]  # Remove =
        
        # Replace cell references with values
        def replace_cell(match):
            ref = match.group(0)
            col = ord(ref[0].upper()) - ord('A')
            row = int(ref[1:]) - 1
            
            if 0 <= row < len(data) and 0 <= col < len(data[0]):
                val = data[row][col]
                if isinstance(val, str) and val.startswith('='):
                    # Recursive evaluation
                    return str(FormulaEngine.evaluate(val, data))
                return str(val) if val else '0'
            return '0'
        
        formula = re.sub(r'[A-Z]\d+', replace_cell, formula, flags=re.IGNORECASE)
        
        # Handle functions
        formula = FormulaEngine.handle_functions(formula, data)
        
        try:
            result = eval(formula, {"__builtins__": {}}, {
                'sum': sum, 'min': min, 'max': max, 'abs': abs,
                'round': round, 'int': int, 'float': float
            })
            return result
        except:
            return "#ERROR"
    
    @staticmethod
    def handle_functions(formula, data):
        """Handle spreadsheet functions"""
        # SUM(A1:A10)
        def sum_range(match):
            range_str = match.group(1)
            cells = FormulaEngine.parse_range(range_str, data)
            return str(sum(cells))
        
        # AVERAGE(A1:A10)
        def avg_range(match):
            range_str = match.group(1)
            cells = FormulaEngine.parse_range(range_str, data)
            return str(sum(cells) / len(cells)) if cells else '0'
        
        # COUNT(A1:A10)
        def count_range(match):
            range_str = match.group(1)
            cells = FormulaEngine.parse_range(range_str, data)
            return str(len([c for c in cells if c != 0]))
        
        formula = re.sub(r'SUM\(([^)]+)\)', sum_range, formula, flags=re.IGNORECASE)
        formula = re.sub(r'AVERAGE\(([^)]+)\)', avg_range, formula, flags=re.IGNORECASE)
        formula = re.sub(r'AVG\(([^)]+)\)', avg_range, formula, flags=re.IGNORECASE)
        formula = re.sub(r'COUNT\(([^)]+)\)', count_range, formula, flags=re.IGNORECASE)
        
        return formula
    
    @staticmethod
    def parse_range(range_str, data):
        """Parse cell range like A1:A10"""
        parts = range_str.split(':')
        if len(parts) != 2:
            return []
        
        start, end = parts
        start_col = ord(start[0].upper()) - ord('A')
        start_row = int(start[1:]) - 1
        end_col = ord(end[0].upper()) - ord('A')
        end_row = int(end[1:]) - 1
        
        cells = []
        for r in range(start_row, end_row + 1):
            for c in range(start_col, end_col + 1):
                if 0 <= r < len(data) and 0 <= c < len(data[0]):
                    val = data[r][c]
                    try:
                        cells.append(float(val) if val else 0)
                    except:
                        pass
        return cells


class Spreadsheet(QWidget):
    """Spreadsheet module"""
    
    back_requested = pyqtSignal()
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.current_file = None
        self.is_modified = False
        self.rows = 100
        self.cols = 26  # A-Z
        self.setup_ui()
        self.setup_shortcuts()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Top bar
        top_bar = self.create_top_bar()
        layout.addWidget(top_bar)
        
        # Toolbar
        toolbar = self.create_toolbar()
        layout.addWidget(toolbar)
        
        # Formula bar
        formula_bar = self.create_formula_bar()
        layout.addWidget(formula_bar)
        
        # Spreadsheet container
        sheet_container = QFrame()
        sheet_container.setStyleSheet(f"background: {COLORS['bg_secondary']}; padding: 10px;")
        sheet_layout = QVBoxLayout(sheet_container)
        
        # Table widget
        self.table = QTableWidget(self.rows, self.cols)
        self.table.setStyleSheet("""
            QTableWidget {
                background: white;
                gridline-color: #d1d5db;
                border: 1px solid #d1d5db;
            }
            QTableWidget::item {
                padding: 5px;
            }
            QHeaderView::section {
                background: #f3f4f6;
                padding: 5px;
                border: 1px solid #d1d5db;
                font-weight: bold;
            }
        """)
        
        # Set column headers (A, B, C, ...)
        headers = [chr(65 + i) for i in range(self.cols)]
        self.table.setHorizontalHeaderLabels(headers)
        
        # Set row headers (1, 2, 3, ...)
        self.table.setVerticalHeaderLabels([str(i + 1) for i in range(self.rows)])
        
        # Resize columns
        for i in range(self.cols):
            self.table.setColumnWidth(i, 100)
        
        self.table.itemChanged.connect(self.on_cell_changed)
        self.table.itemSelectionChanged.connect(self.on_selection_changed)
        
        sheet_layout.addWidget(self.table)
        layout.addWidget(sheet_container)
        
        # Status bar
        status_bar = self.create_status_bar()
        layout.addWidget(status_bar)
    
    def create_top_bar(self):
        """Create top navigation bar"""
        bar = QFrame()
        bar.setFixedHeight(60)
        bar.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['accent_sheet']};
                border-bottom: 1px solid rgba(255, 255, 255, 0.1);
            }}
        """)
        
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(20, 10, 20, 10)
        
        # Back button
        back_btn = QPushButton("← Back")
        back_btn.setFixedHeight(40)
        back_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        back_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.2);
                color: white;
                border: none;
                border-radius: 8px;
                padding: 0 20px;
                font-size: 13px;
                font-weight: bold;
            }
            QPushButton:hover { background: rgba(255, 255, 255, 0.3); }
        """)
        back_btn.clicked.connect(self.go_back)
        layout.addWidget(back_btn)
        
        # Title
        title = QLabel("📊 Spreadsheet")
        title.setStyleSheet("color: white; font-size: 20px; font-weight: bold; margin-left: 20px;")
        layout.addWidget(title)
        
        layout.addStretch()
        
        # Document name
        self.doc_name = QLabel("Untitled Spreadsheet")
        self.doc_name.setStyleSheet("color: white; font-size: 14px;")
        layout.addWidget(self.doc_name)
        
        return bar
    
    def create_toolbar(self):
        """Create toolbar"""
        toolbar = QFrame()
        toolbar.setFixedHeight(60)
        toolbar.setStyleSheet(f"background: {COLORS['bg_tertiary']};")
        
        layout = QHBoxLayout(toolbar)
        layout.setContentsMargins(20, 10, 20, 10)
        layout.setSpacing(10)
        
        # File operations
        new_btn = self.create_tool_button("📄 New", self.new_spreadsheet)
        layout.addWidget(new_btn)
        
        open_btn = self.create_tool_button("📂 Open", self.open_spreadsheet)
        layout.addWidget(open_btn)
        
        save_btn = self.create_tool_button("💾 Save", self.save_spreadsheet)
        layout.addWidget(save_btn)
        
        layout.addWidget(self.create_separator())
        
        # Row/Column operations
        add_row_btn = self.create_tool_button("+ Row", self.add_row)
        layout.addWidget(add_row_btn)
        
        add_col_btn = self.create_tool_button("+ Column", self.add_column)
        layout.addWidget(add_col_btn)
        
        del_row_btn = self.create_tool_button("- Row", self.delete_row)
        layout.addWidget(del_row_btn)
        
        del_col_btn = self.create_tool_button("- Column", self.delete_column)
        layout.addWidget(del_col_btn)
        
        layout.addWidget(self.create_separator())
        
        # Formatting
        bold_btn = self.create_tool_button("B", self.toggle_bold)
        bold_btn.setFont(QFont("Arial", 10, QFont.Weight.Bold))
        layout.addWidget(bold_btn)
        
        # Background color
        bg_color_btn = self.create_tool_button("🎨 BG", self.change_bg_color)
        layout.addWidget(bg_color_btn)
        
        layout.addWidget(self.create_separator())
        
        # Calculate
        calc_btn = self.create_tool_button("⚡ Calculate", self.recalculate)
        layout.addWidget(calc_btn)
        
        layout.addStretch()
        
        return toolbar
    
    def create_formula_bar(self):
        """Create formula bar"""
        bar = QFrame()
        bar.setFixedHeight(50)
        bar.setStyleSheet(f"background: {COLORS['bg_tertiary']}; border-bottom: 1px solid rgba(255, 255, 255, 0.1);")
        
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(20, 10, 20, 10)
        
        # Cell reference
        self.cell_ref = QLabel("A1")
        self.cell_ref.setFixedWidth(60)
        self.cell_ref.setStyleSheet(f"""
            color: {COLORS['text_primary']};
            font-weight: bold;
            font-size: 13px;
            background: rgba(255, 255, 255, 0.1);
            border: 1px solid rgba(255, 255, 255, 0.2);
            border-radius: 4px;
            padding: 5px;
        """)
        layout.addWidget(self.cell_ref)
        
        # Formula input
        self.formula_input = QLineEdit()
        self.formula_input.setPlaceholderText("Enter formula or value...")
        self.formula_input.setStyleSheet(f"""
            QLineEdit {{
                background: rgba(255, 255, 255, 0.1);
                color: {COLORS['text_primary']};
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 4px;
                padding: 5px 10px;
                font-size: 13px;
            }}
        """)
        self.formula_input.returnPressed.connect(self.apply_formula)
        layout.addWidget(self.formula_input)
        
        return bar
    
    def create_status_bar(self):
        """Create status bar"""
        bar = QFrame()
        bar.setFixedHeight(40)
        bar.setStyleSheet(f"background: {COLORS['bg_tertiary']};")
        
        layout = QHBoxLayout(bar)
        layout.setContentsMargins(20, 5, 20, 5)
        
        self.status_label = QLabel("Ready")
        self.status_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(self.status_label)
        
        layout.addStretch()
        
        self.cell_info = QLabel("")
        self.cell_info.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 12px;")
        layout.addWidget(self.cell_info)
        
        return bar
    
    def create_tool_button(self, text, callback):
        """Create toolbar button"""
        btn = QPushButton(text)
        btn.setFixedSize(80, 35)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setStyleSheet(f"""
            QPushButton {{
                background: rgba(255, 255, 255, 0.1);
                color: {COLORS['text_primary']};
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 6px;
                font-size: 11px;
            }}
            QPushButton:hover {{
                background: rgba(5, 150, 105, 0.3);
            }}
        """)
        btn.clicked.connect(callback)
        return btn
    
    def create_separator(self):
        """Create separator"""
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.VLine)
        sep.setStyleSheet("background: rgba(255, 255, 255, 0.2); max-width: 1px;")
        return sep
    
    def setup_shortcuts(self):
        """Setup keyboard shortcuts"""
        from PyQt6.QtGui import QShortcut, QKeySequence
        
        QShortcut(QKeySequence.StandardKey.New, self, self.new_spreadsheet)
        QShortcut(QKeySequence.StandardKey.Open, self, self.open_spreadsheet)
        QShortcut(QKeySequence.StandardKey.Save, self, self.save_spreadsheet)
    
    def go_back(self):
        """Return to launcher"""
        if self.is_modified:
            reply = QMessageBox.question(
                self, "Unsaved Changes",
                "Do you want to save changes before going back?",
                QMessageBox.StandardButton.Save | 
                QMessageBox.StandardButton.Discard | 
                QMessageBox.StandardButton.Cancel
            )
            
            if reply == QMessageBox.StandardButton.Save:
                self.save_spreadsheet()
                self.back_requested.emit()
            elif reply == QMessageBox.StandardButton.Discard:
                self.back_requested.emit()
        else:
            self.back_requested.emit()
    
    def on_cell_changed(self, item):
        """Handle cell content change"""
        self.is_modified = True
        self.recalculate()
    
    def on_selection_changed(self):
        """Handle cell selection change"""
        current = self.table.currentItem()
        if current:
            row = current.row()
            col = current.column()
            col_letter = chr(65 + col)
            self.cell_ref.setText(f"{col_letter}{row + 1}")
            
            # Show formula in formula bar
            text = current.text() if current.text() else ""
            self.formula_input.setText(text)
    
    def apply_formula(self):
        """Apply formula from formula bar"""
        current = self.table.currentItem()
        if current:
            formula = self.formula_input.text()
            current.setText(formula)
            self.recalculate()
    
    def get_cell_data(self):
        """Get all cell data as 2D array"""
        data = []
        for r in range(self.table.rowCount()):
            row = []
            for c in range(self.table.columnCount()):
                item = self.table.item(r, c)
                row.append(item.text() if item else "")
            data.append(row)
        return data
    
    def recalculate(self):
        """Recalculate all formulas"""
        data = self.get_cell_data()
        
        for r in range(self.table.rowCount()):
            for c in range(self.table.columnCount()):
                item = self.table.item(r, c)
                if item and item.text().startswith('='):
                    result = FormulaEngine.evaluate(item.text(), data)
                    item.setData(Qt.ItemDataRole.UserRole, item.text())  # Store formula
                    item.setText(str(result))
                    
                    # Color error cells
                    if result == "#ERROR":
                        item.setBackground(QBrush(QColor("#fee2e2")))
                    else:
                        item.setBackground(QBrush(QColor("white")))
        
        self.status_label.setText("Calculations updated")
        play_sound("click.wav")
    
    def new_spreadsheet(self):
        """Create new spreadsheet"""
        if self.is_modified:
            reply = QMessageBox.question(
                self, "Unsaved Changes",
                "Do you want to save changes?",
                QMessageBox.StandardButton.Save | 
                QMessageBox.StandardButton.Discard | 
                QMessageBox.StandardButton.Cancel
            )
            
            if reply == QMessageBox.StandardButton.Save:
                self.save_spreadsheet()
            elif reply == QMessageBox.StandardButton.Cancel:
                return
        
        self.table.clear()
        self.current_file = None
        self.is_modified = False
        self.doc_name.setText("Untitled Spreadsheet")
        play_sound("click.wav")
    
    def open_spreadsheet(self):
        """Open spreadsheet"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Open Spreadsheet", "",
            "JSON Files (*.json);;CSV Files (*.csv);;All Files (*.*)"
        )
        
        if file_path:
            try:
                if file_path.endswith('.json'):
                    with open(file_path, 'r') as f:
                        data = json.load(f)
                        self.load_data(data)
                elif file_path.endswith('.csv'):
                    with open(file_path, 'r') as f:
                        import csv
                        reader = csv.reader(f)
                        data = list(reader)
                        self.load_data(data)
                
                self.current_file = file_path
                self.is_modified = False
                self.doc_name.setText(Path(file_path).name)
                play_sound("success.wav")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to open file: {str(e)}")
    
    def load_data(self, data):
        """Load data into table"""
        for r, row in enumerate(data):
            for c, value in enumerate(row):
                if r < self.table.rowCount() and c < self.table.columnCount():
                    item = QTableWidgetItem(str(value))
                    self.table.setItem(r, c, item)
        self.recalculate()
    
    def save_spreadsheet(self):
        """Save spreadsheet"""
        if self.current_file:
            self.save_to_file(self.current_file)
        else:
            self.save_as_spreadsheet()
    
    def save_as_spreadsheet(self):
        """Save spreadsheet as"""
        file_path, _ = QFileDialog.getSaveFileName(
            self, "Save Spreadsheet", "",
            "JSON Files (*.json);;CSV Files (*.csv);;All Files (*.*)"
        )
        
        if file_path:
            self.save_to_file(file_path)
    
    def save_to_file(self, file_path):
        """Save to file"""
        try:
            data = self.get_cell_data()
            
            if file_path.endswith('.json'):
                with open(file_path, 'w') as f:
                    json.dump(data, f, indent=2)
            elif file_path.endswith('.csv'):
                import csv
                with open(file_path, 'w', newline='') as f:
                    writer = csv.writer(f)
                    writer.writerows(data)
            
            self.current_file = file_path
            self.is_modified = False
            self.doc_name.setText(Path(file_path).name)
            play_sound("success.wav")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to save file: {str(e)}")
    
    def add_row(self):
        """Add new row"""
        self.table.insertRow(self.table.rowCount())
        play_sound("click.wav")
    
    def add_column(self):
        """Add new column"""
        self.table.insertColumn(self.table.columnCount())
        col_idx = self.table.columnCount() - 1
        self.table.setHorizontalHeaderItem(col_idx, QTableWidgetItem(chr(65 + col_idx)))
        play_sound("click.wav")
    
    def delete_row(self):
        """Delete selected row"""
        current = self.table.currentRow()
        if current >= 0:
            self.table.removeRow(current)
            play_sound("click.wav")
    
    def delete_column(self):
        """Delete selected column"""
        current = self.table.currentColumn()
        if current >= 0:
            self.table.removeColumn(current)
            play_sound("click.wav")
    
    def toggle_bold(self):
        """Toggle bold formatting"""
        current = self.table.currentItem()
        if current:
            font = current.font()
            font.setBold(not font.bold())
            current.setFont(font)
    
    def change_bg_color(self):
        """Change cell background color"""
        from PyQt6.QtWidgets import QColorDialog
        color = QColorDialog.getColor()
        if color.isValid():
            for item in self.table.selectedItems():
                item.setBackground(QBrush(color))