"""
YouOS 10 PyQt6 - Widgets Module
widgets.py - All widget components (Clock, Battery, Weather, Calendar, System Monitor)
"""

import math
import calendar
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QGridLayout, QFrame, QSlider,
                              QGraphicsDropShadowEffect)
from PyQt6.QtCore import Qt, QTimer, QTime, QDate
from PyQt6.QtGui import QPainter, QPen, QColor, QBrush, QPainterPath, QLinearGradient

# Import utils
try:
    from utils import (get_battery_info, get_weather, get_volume, 
                      set_volume, get_system_stats)
except ImportError:
    def get_battery_info():
        return {'percent': 85, 'plugged': False, 'time_left': 3600}
    def get_weather(city="Cairo"):
        return {'temp_c': '22', 'temp_f': '72', 'description': 'Partly Cloudy', 'icon': '🌤️'}
    def get_volume():
        return 50
    def set_volume(val):
        pass
    def get_system_stats():
        return {'cpu': 45, 'ram': 60, 'temp': 55}

COLORS = {
    'bg_primary': "#24244B",
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
    """Frame with glassmorphism effect"""
    
    def __init__(self, parent=None, opacity=0.1):
        super().__init__(parent)
        self.opacity = opacity
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(30)
        shadow.setColor(QColor(0, 0, 0, 100))
        shadow.setOffset(0, 5)
        self.setGraphicsEffect(shadow)
    
    def paintEvent(self, event):
        from PyQt6.QtCore import QRectF
        
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        path = QPainterPath()
        path.addRoundedRect(QRectF(self.rect()), 16, 16)
        
        # Enhanced background with more opacity for better visibility
        gradient = QLinearGradient(0, 0, 0, self.height())
        gradient.setColorAt(0, QColor(30, 30, 60, 220))  # Darker, more opaque
        gradient.setColorAt(1, QColor(20, 20, 50, 200))
        
        painter.fillPath(path, gradient)
        painter.setPen(QPen(QColor(255, 255, 255, 80), 1))
        painter.drawPath(path)


class AnalogClock(QWidget):
    """Analog clock widget"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(200, 200)
        
        self.timer = QTimer()
        self.timer.timeout.connect(self.update)
        self.timer.start(1000)
    
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        center_x = self.width() / 2
        center_y = self.height() / 2
        radius = min(self.width(), self.height()) / 2 - 10
        
        # Clock circle
        painter.setPen(QPen(QColor(59, 130, 246), 3))
        painter.drawEllipse(int(center_x - radius), int(center_y - radius),
                          int(radius * 2), int(radius * 2))
        
        # Hour markers
        for i in range(12):
            angle = math.radians(i * 30 - 90)
            x1 = center_x + (radius - 20) * math.cos(angle)
            y1 = center_y + (radius - 20) * math.sin(angle)
            x2 = center_x + radius * math.cos(angle)
            y2 = center_y + radius * math.sin(angle)
            
            width = 3 if i % 3 == 0 else 2
            painter.setPen(QPen(QColor(156, 163, 175), width))
            painter.drawLine(int(x1), int(y1), int(x2), int(y2))
        
        # Get time
        time = QTime.currentTime()
        hour = time.hour() % 12
        minute = time.minute()
        second = time.second()
        
        # Hour hand
        hour_angle = math.radians((hour + minute / 60) * 30 - 90)
        hour_x = center_x + radius * 0.5 * math.cos(hour_angle)
        hour_y = center_y + radius * 0.5 * math.sin(hour_angle)
        painter.setPen(QPen(Qt.GlobalColor.white, 6, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawLine(int(center_x), int(center_y), int(hour_x), int(hour_y))
        
        # Minute hand
        minute_angle = math.radians(minute * 6 - 90)
        minute_x = center_x + radius * 0.7 * math.cos(minute_angle)
        minute_y = center_y + radius * 0.7 * math.sin(minute_angle)
        painter.setPen(QPen(Qt.GlobalColor.white, 4, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawLine(int(center_x), int(center_y), int(minute_x), int(minute_y))
        
        # Second hand
        second_angle = math.radians(second * 6 - 90)
        second_x = center_x + radius * 0.8 * math.cos(second_angle)
        second_y = center_y + radius * 0.8 * math.sin(second_angle)
        painter.setPen(QPen(QColor(59, 130, 246), 2, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawLine(int(center_x), int(center_y), int(second_x), int(second_y))
        
        # Center dot
        painter.setBrush(QBrush(QColor(59, 130, 246)))
        painter.drawEllipse(int(center_x - 5), int(center_y - 5), 10, 10)


class BatteryWidget(GlassFrame):
    """Battery indicator widget"""
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.15)
        self.setup_ui()
        
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_battery)
        self.timer.start(30000)
        
        self.update_battery()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 15, 20, 15)
        layout.setSpacing(10)
        
        header = QHBoxLayout()
        
        self.battery_icon = QLabel("🔋")
        self.battery_icon.setStyleSheet("font-size: 24px;")
        header.addWidget(self.battery_icon)
        
        header.addSpacing(10)
        
        info_layout = QVBoxLayout()
        info_layout.setSpacing(2)
        
        self.battery_percent = QLabel("85%")
        self.battery_percent.setStyleSheet("color: white; font-size: 20px; font-weight: bold;")
        info_layout.addWidget(self.battery_percent)
        
        self.battery_status = QLabel("On Battery")
        self.battery_status.setStyleSheet("color: #9ca3af; font-size: 11px;")
        info_layout.addWidget(self.battery_status)
        
        header.addLayout(info_layout)
        header.addStretch()
        
        layout.addLayout(header)
        
        self.battery_bar = QFrame()
        self.battery_bar.setFixedHeight(8)
        self.battery_bar.setStyleSheet("""
            QFrame {
                background: rgba(255, 255, 255, 0.1);
                border-radius: 4px;
            }
        """)
        layout.addWidget(self.battery_bar)
        
        self.time_remaining = QLabel("2h 30m remaining")
        self.time_remaining.setStyleSheet("color: #9ca3af; font-size: 10px;")
        layout.addWidget(self.time_remaining)
    
    def update_battery(self):
        battery_info = get_battery_info()
        
        if battery_info:
            percent = battery_info['percent']
            plugged = battery_info['plugged']
            time_left = battery_info.get('time_left')
            
            self.battery_percent.setText(f"{percent}%")
            
            if plugged:
                self.battery_icon.setText("⚡")
                self.battery_status.setText("Charging")
                self.time_remaining.setText("Plugged in")
            else:
                if percent > 80:
                    self.battery_icon.setText("🔋")
                elif percent > 50:
                    self.battery_icon.setText("🔋")
                elif percent > 20:
                    self.battery_icon.setText("🪫")
                else:
                    self.battery_icon.setText("🪫")
                
                self.battery_status.setText("On Battery")
                
                if time_left and time_left > 0:
                    hours = time_left // 3600
                    minutes = (time_left % 3600) // 60
                    self.time_remaining.setText(f"{hours}h {minutes}m remaining")
                else:
                    self.time_remaining.setText("Calculating...")
            
            if percent > 50:
                color = COLORS['success']
            elif percent > 20:
                color = COLORS['warning']
            else:
                color = COLORS['error']
            
            self.battery_bar.setStyleSheet(f"""
                QFrame {{
                    background: qlineargradient(
                        x1:0, y1:0, x2:1, y2:0,
                        stop:0 {color},
                        stop:{percent/100} {color},
                        stop:{percent/100} rgba(255, 255, 255, 0.1),
                        stop:1 rgba(255, 255, 255, 0.1)
                    );
                    border-radius: 4px;
                }}
            """)


class SystemMonitorWidget(GlassFrame):
    """System monitor showing CPU, RAM, and Temperature"""
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.15)
        self.setup_ui()
        
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_stats)
        self.timer.start(2000)
        
        self.update_stats()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 15, 20, 15)
        layout.setSpacing(15)
        
        title = QLabel("System Monitor")
        title.setStyleSheet("color: white; font-size: 14px; font-weight: bold;")
        layout.addWidget(title)
        
        # CPU
        self.cpu_label = QLabel("CPU: 0%")
        self.cpu_label.setStyleSheet("color: #9ca3af; font-size: 12px;")
        layout.addWidget(self.cpu_label)
        
        self.cpu_bar = QFrame()
        self.cpu_bar.setFixedHeight(6)
        layout.addWidget(self.cpu_bar)
        
        # RAM
        self.ram_label = QLabel("RAM: 0%")
        self.ram_label.setStyleSheet("color: #9ca3af; font-size: 12px;")
        layout.addWidget(self.ram_label)
        
        self.ram_bar = QFrame()
        self.ram_bar.setFixedHeight(6)
        layout.addWidget(self.ram_bar)
        
        # Temperature
        self.temp_label = QLabel("🌡️ Temperature: --°C")
        self.temp_label.setStyleSheet("color: #9ca3af; font-size: 12px;")
        layout.addWidget(self.temp_label)
        
        self.temp_bar = QFrame()
        self.temp_bar.setFixedHeight(6)
        layout.addWidget(self.temp_bar)
    
    def update_stats(self):
        stats = get_system_stats()
        
        cpu = stats.get('cpu', 0)
        ram = stats.get('ram', 0)
        temp = stats.get('temp', 0)
        
        self.cpu_label.setText(f"CPU: {cpu}%")
        self.update_bar(self.cpu_bar, cpu, COLORS['accent_primary'])
        
        self.ram_label.setText(f"RAM: {ram}%")
        self.update_bar(self.ram_bar, ram, COLORS['success'])
        
        if temp > 0:
            self.temp_label.setText(f"🌡️ Temperature: {temp}°C")
            if temp > 80:
                color = COLORS['error']
            elif temp > 65:
                color = COLORS['warning']
            else:
                color = COLORS['success']
            self.update_bar(self.temp_bar, min(temp, 100), color)
        else:
            self.temp_label.setText("🌡️ Temperature: N/A")
            self.update_bar(self.temp_bar, 0, COLORS['text_secondary'])
    
    def update_bar(self, bar, percent, color):
        bar.setStyleSheet(f"""
            QFrame {{
                background: qlineargradient(
                    x1:0, y1:0, x2:1, y2:0,
                    stop:0 {color},
                    stop:{percent/100} {color},
                    stop:{percent/100} rgba(255, 255, 255, 0.1),
                    stop:1 rgba(255, 255, 255, 0.1)
                );
                border-radius: 3px;
            }}
        """)


class CalendarWidget(GlassFrame):
    """Calendar widget with month view"""
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.15)
        self.current_date = QDate.currentDate()
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 15, 20, 15)
        layout.setSpacing(10)
        
        header = QHBoxLayout()
        
        prev_btn = QPushButton("◀")
        prev_btn.setFixedSize(30, 30)
        prev_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        prev_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.1);
                border: none;
                border-radius: 6px;
                color: white;
                font-size: 12px;
            }
            QPushButton:hover {
                background: rgba(255, 255, 255, 0.2);
            }
        """)
        prev_btn.clicked.connect(self.prev_month)
        header.addWidget(prev_btn)
        
        self.month_year_label = QLabel()
        self.month_year_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.month_year_label.setStyleSheet("color: white; font-size: 14px; font-weight: bold;")
        header.addWidget(self.month_year_label, stretch=1)
        
        next_btn = QPushButton("▶")
        next_btn.setFixedSize(30, 30)
        next_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        next_btn.setStyleSheet("""
            QPushButton {
                background: rgba(255, 255, 255, 0.1);
                border: none;
                border-radius: 6px;
                color: white;
                font-size: 12px;
            }
            QPushButton:hover {
                background: rgba(255, 255, 255, 0.2);
            }
        """)
        next_btn.clicked.connect(self.next_month)
        header.addWidget(next_btn)
        
        layout.addLayout(header)
        
        days_header = QHBoxLayout()
        days_header.setSpacing(5)
        
        for day in ['M', 'T', 'W', 'T', 'F', 'S', 'S']:
            label = QLabel(day)
            label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            label.setStyleSheet("color: #9ca3af; font-size: 11px; font-weight: bold;")
            label.setFixedSize(40, 20)
            days_header.addWidget(label)
        
        layout.addLayout(days_header)
        
        self.calendar_grid = QGridLayout()
        self.calendar_grid.setSpacing(5)
        
        self.day_buttons = []
        for row in range(6):
            for col in range(7):
                btn = QPushButton()
                btn.setFixedSize(40, 35)
                btn.setCursor(Qt.CursorShape.PointingHandCursor)
                btn.setStyleSheet("""
                    QPushButton {
                        background: transparent;
                        border: none;
                        border-radius: 6px;
                        color: white;
                        font-size: 12px;
                    }
                    QPushButton:hover {
                        background: rgba(255, 255, 255, 0.1);
                    }
                """)
                self.calendar_grid.addWidget(btn, row, col)
                self.day_buttons.append(btn)
        
        layout.addLayout(self.calendar_grid)
        self.update_calendar()
    
    def update_calendar(self):
        year = self.current_date.year()
        month = self.current_date.month()
        
        self.month_year_label.setText(f"{self.current_date.toString('MMMM yyyy')}")
        
        cal = calendar.monthcalendar(year, month)
        today = QDate.currentDate()
        
        days = []
        for week in cal:
            days.extend(week)
        
        for i, btn in enumerate(self.day_buttons):
            if i < len(days) and days[i] != 0:
                day = days[i]
                btn.setText(str(day))
                btn.setVisible(True)
                
                is_today = (year == today.year() and 
                           month == today.month() and 
                           day == today.day())
                
                if is_today:
                    btn.setStyleSheet("""
                        QPushButton {
                            background: rgba(59, 130, 246, 0.5);
                            border: none;
                            border-radius: 6px;
                            color: white;
                            font-size: 12px;
                            font-weight: bold;
                        }
                        QPushButton:hover {
                            background: rgba(59, 130, 246, 0.7);
                        }
                    """)
                else:
                    btn.setStyleSheet("""
                        QPushButton {
                            background: transparent;
                            border: none;
                            border-radius: 6px;
                            color: white;
                            font-size: 12px;
                        }
                        QPushButton:hover {
                            background: rgba(255, 255, 255, 0.1);
                        }
                    """)
            else:
                btn.setText("")
                btn.setVisible(False)
    
    def prev_month(self):
        self.current_date = self.current_date.addMonths(-1)
        self.update_calendar()
    
    def next_month(self):
        self.current_date = self.current_date.addMonths(1)
        self.update_calendar()


class WeatherWidget(GlassFrame):
    """Weather widget with real data"""
    
    def __init__(self, parent=None):
        super().__init__(parent, opacity=0.15)
        self.setup_ui()
        
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_weather)
        self.timer.start(1800000)
        
        self.update_weather()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(10)
        
        self.weather_icon = QLabel("⏳")
        self.weather_icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.weather_icon.setStyleSheet("font-size: 48px;")
        layout.addWidget(self.weather_icon)
        
        self.temp_label = QLabel("Loading...")
        self.temp_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.temp_label.setStyleSheet("color: white; font-size: 20px; font-weight: bold;")
        layout.addWidget(self.temp_label)
        
        self.desc_label = QLabel("")
        self.desc_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.desc_label.setStyleSheet("color: #9ca3af; font-size: 12px;")
        self.desc_label.setWordWrap(True)
        layout.addWidget(self.desc_label)
        
        self.info_label = QLabel("")
        self.info_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.info_label.setStyleSheet("color: #9ca3af; font-size: 10px;")
        layout.addWidget(self.info_label)
    
    def update_weather(self):
        try:
            weather = get_weather("Cairo")
            
            self.weather_icon.setText(weather['icon'])
            self.temp_label.setText(f"{weather['temp_c']}°C / {weather['temp_f']}°F")
            self.desc_label.setText(weather['description'])
            
            info_parts = []
            if 'humidity' in weather:
                info_parts.append(f"💧 {weather['humidity']}%")
            if 'wind_speed' in weather:
                info_parts.append(f"💨 {weather['wind_speed']} km/h")
            
            self.info_label.setText(" • ".join(info_parts))
            
        except Exception as e:
            print(f"⚠️  Weather update failed: {e}")
            self.weather_icon.setText("❌")
            self.temp_label.setText("Connection Error")
            self.desc_label.setText("Can't load weather data")


class BatteryIndicator(QWidget):
    """Battery indicator for taskbar"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setup_ui()
        
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_battery)
        self.timer.start(1000)  # Update every second
        
        self.update_battery()
    
    def setup_ui(self):
        layout = QHBoxLayout(self)
        layout.setContentsMargins(8, 0, 8, 0)
        layout.setSpacing(5)
        
        self.battery_icon = QLabel("🔋")
        self.battery_icon.setStyleSheet("font-size: 16px;")
        layout.addWidget(self.battery_icon)
        
        self.battery_text = QLabel("85%")
        self.battery_text.setStyleSheet("color: white; font-size: 12px; font-weight: 500;")
        layout.addWidget(self.battery_text)
    
    def update_battery(self):
        battery_info = get_battery_info()
        
        if battery_info:
            percent = battery_info['percent']
            plugged = battery_info['plugged']
            
            self.battery_text.setText(f"{percent}%")
            
            if plugged:
                self.battery_icon.setText("⚡")
            else:
                if percent > 80:
                    self.battery_icon.setText("🔋")
                elif percent > 50:
                    self.battery_icon.setText("🔋")
                elif percent > 20:
                    self.battery_icon.setText("🪫")
                else:
                    self.battery_icon.setText("🪫")
