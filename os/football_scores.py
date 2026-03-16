"""
YouOS 10 - Live Football Scores Module (Crash-Proof Version)
Complete thread safety and proper cleanup to prevent crashes
"""

import sys
import json
import requests
from pathlib import Path
from datetime import datetime, timedelta
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QScrollArea, QLineEdit, QListWidget,
                              QListWidgetItem, QFrame, QGraphicsDropShadowEffect,
                              QApplication)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QThread, QPoint, QSize, QMutex
from PyQt6.QtGui import QColor

# Configuration
BASE_DIR = Path(__file__).parent
SCORES_DATA_FILE = Path.home() / '.youos' / 'football_scores.json'
SCORES_DATA_FILE.parent.mkdir(exist_ok=True)

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
    'live': '#ef4444',
}


class FootballAPIClient:
    """Client using TheSportsDB free API"""
    
    def __init__(self):
        self.base_url = "https://www.thesportsdb.com/api/v1/json/3"
    
    def search_teams(self, query):
        """Search for teams"""
        try:
            url = f"{self.base_url}/searchteams.php?t={query}"
            response = requests.get(url, timeout=5)
            
            if response.status_code == 200:
                data = response.json()
                teams = data.get('teams', [])
                
                if teams:
                    result = []
                    for team in teams:
                        if team.get('strSport') == 'Soccer':
                            result.append({
                                'id': team['idTeam'],
                                'name': team['strTeam'],
                                'country': team.get('strCountry', 'Unknown'),
                                'logo': team.get('strTeamBadge', ''),
                                'league': team.get('strLeague', '')
                            })
                    return result[:10]
            return []
        except Exception as e:
            print(f"⚽ Search error: {e}")
            return []
    
    def get_team_matches(self, team_id, team_name):
        """Get matches ONLY for this specific team"""
        matches = []
        
        try:
            # Get recent results
            url_last = f"{self.base_url}/eventslast.php?id={team_id}"
            response = requests.get(url_last, timeout=5)
            
            if response.status_code == 200:
                data = response.json()
                results = data.get('results', [])
                
                for event in results[:5]:
                    home_team = event.get('strHomeTeam', '')
                    away_team = event.get('strAwayTeam', '')
                    
                    if home_team == team_name or away_team == team_name:
                        matches.append({
                            'id': event.get('idEvent'),
                            'home_team': home_team,
                            'away_team': away_team,
                            'home_score': event.get('intHomeScore'),
                            'away_score': event.get('intAwayScore'),
                            'status': 'FT',
                            'league': event.get('strLeague', ''),
                            'date': event.get('dateEvent', ''),
                            'time': '',
                            'is_live': False
                        })
        except:
            pass
        
        try:
            # Get upcoming matches
            url_next = f"{self.base_url}/eventsnext.php?id={team_id}"
            response = requests.get(url_next, timeout=5)
            
            if response.status_code == 200:
                data = response.json()
                events = data.get('events', [])
                
                for event in events[:5]:
                    home_team = event.get('strHomeTeam', '')
                    away_team = event.get('strAwayTeam', '')
                    
                    if home_team == team_name or away_team == team_name:
                        matches.append({
                            'id': event.get('idEvent'),
                            'home_team': home_team,
                            'away_team': away_team,
                            'home_score': event.get('intHomeScore'),
                            'away_score': event.get('intAwayScore'),
                            'status': 'Scheduled',
                            'league': event.get('strLeague', ''),
                            'date': event.get('dateEvent', ''),
                            'time': event.get('strTime', ''),
                            'is_live': False
                        })
        except:
            pass
        
        return matches
    
    def get_live_matches(self, team_data):
        """Get all matches for all followed teams"""
        all_matches = []
        
        for team_id, team_name in team_data:
            team_matches = self.get_team_matches(team_id, team_name)
            all_matches.extend(team_matches)
        
        # Remove duplicates
        unique_matches = []
        seen_ids = set()
        
        for match in all_matches:
            match_id = match.get('id')
            if match_id and match_id not in seen_ids:
                seen_ids.add(match_id)
                unique_matches.append(match)
        
        # Sort
        def sort_key(match):
            if match['status'] == 'FT':
                try:
                    return (0, datetime.strptime(match['date'], '%Y-%m-%d'))
                except:
                    return (0, datetime.min)
            else:
                try:
                    return (1, datetime.strptime(match['date'], '%Y-%m-%d'))
                except:
                    return (1, datetime.max)
        
        unique_matches.sort(key=sort_key, reverse=True)
        return unique_matches[:15]


class LiveScoreUpdateThread(QThread):
    """Background update thread with proper cleanup"""
    
    scores_updated = pyqtSignal(list)
    
    def __init__(self, api_client, team_data):
        super().__init__()
        self.api_client = api_client
        self.team_data = team_data
        self.running = True
        self.mutex = QMutex()
    
    def run(self):
        while True:
            self.mutex.lock()
            should_run = self.running
            self.mutex.unlock()
            
            if not should_run:
                break
            
            try:
                matches = self.api_client.get_live_matches(self.team_data)
                self.scores_updated.emit(matches)
            except:
                pass
            
            # Sleep in small increments so we can stop quickly
            for _ in range(60):  # 60 seconds total
                self.mutex.lock()
                should_run = self.running
                self.mutex.unlock()
                
                if not should_run:
                    break
                    
                self.msleep(1000)  # 1 second
    
    def stop(self):
        """Stop the thread safely"""
        self.mutex.lock()
        self.running = False
        self.mutex.unlock()


class LiveScoreWidget(QFrame):
    """Individual match display widget"""
    
    clicked = pyqtSignal(dict)
    
    def __init__(self, match_data, parent=None):
        super().__init__(parent)
        self.match_data = match_data
        self.setup_ui()
        self.update_match(match_data)
    
    def setup_ui(self):
        self.setFixedHeight(80)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['bg_secondary']};
                border: 1px solid {COLORS['border']};
                border-radius: 12px;
            }}
            QFrame:hover {{
                background: {COLORS['bg_tertiary']};
                border: 1px solid {COLORS['accent_primary']};
            }}
        """)
        
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(15)
        shadow.setColor(QColor(0, 0, 0, 60))
        shadow.setOffset(0, 4)
        self.setGraphicsEffect(shadow)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)
        layout.setSpacing(5)
        
        top_row = QHBoxLayout()
        
        self.league_label = QLabel()
        self.league_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 10px; font-weight: bold;")
        top_row.addWidget(self.league_label)
        
        top_row.addStretch()
        
        self.status_label = QLabel()
        self.status_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 10px; font-weight: bold;")
        top_row.addWidget(self.status_label)
        
        layout.addLayout(top_row)
        
        main_row = QHBoxLayout()
        main_row.setSpacing(10)
        
        self.home_team_label = QLabel()
        self.home_team_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 13px; font-weight: 600;")
        self.home_team_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        main_row.addWidget(self.home_team_label, stretch=1)
        
        self.score_label = QLabel()
        self.score_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 20px; font-weight: bold;")
        self.score_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        main_row.addWidget(self.score_label)
        
        self.away_team_label = QLabel()
        self.away_team_label.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 13px; font-weight: 600;")
        self.away_team_label.setAlignment(Qt.AlignmentFlag.AlignLeft)
        main_row.addWidget(self.away_team_label, stretch=1)
        
        layout.addLayout(main_row)
    
    def update_match(self, match_data):
        self.league_label.setText(match_data.get('league', '')[:25])
        
        status = match_data.get('status', '')
        date_str = match_data.get('date', '')
        time_str = match_data.get('time', '')
        
        if status == 'FT':
            self.status_label.setText("FULL TIME")
        else:
            if date_str:
                try:
                    dt = datetime.strptime(date_str, '%Y-%m-%d')
                    status_text = dt.strftime('%b %d')
                    if time_str:
                        status_text += f" {time_str}"
                    self.status_label.setText(status_text)
                except:
                    self.status_label.setText(f"{date_str} {time_str}".strip())
            else:
                self.status_label.setText("Scheduled")
        
        self.home_team_label.setText(match_data.get('home_team', '')[:20])
        self.away_team_label.setText(match_data.get('away_team', '')[:20])
        
        home_score = match_data.get('home_score')
        away_score = match_data.get('away_score')
        
        if home_score is not None and away_score is not None:
            self.score_label.setText(f"{home_score} - {away_score}")
        else:
            self.score_label.setText("- : -")


class LiveScoreOverlay(QFrame):
    """Live score overlay with crash-proof cleanup"""
    
    closed = pyqtSignal()
    resized = pyqtSignal()  # emitted after content changes so desktop can reposition
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint)
        
        self.api_client = FootballAPIClient()
        self.followed_teams = self.load_followed_teams()
        self.live_matches = []
        self.score_widgets = []
        self.update_thread = None
        self.is_closing = False
        
        self.setup_ui()
        QTimer.singleShot(200, self.start_updates)
    
    def setup_ui(self):
        # No fixed sizes at all — let content drive the geometry
        self.setStyleSheet(f"""
            QFrame {{
                background: rgba(26, 26, 46, 0.95);
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 16px;
            }}
        """)
        
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(30)
        shadow.setColor(QColor(0, 0, 0, 120))
        shadow.setOffset(0, 8)
        self.setGraphicsEffect(shadow)
        
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(15, 15, 15, 15)
        main_layout.setSpacing(10)
        
        # Header
        header_layout = QHBoxLayout()
        
        title = QLabel("⚽ Live Scores")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 16px; font-weight: bold;")
        header_layout.addWidget(title)
        
        header_layout.addStretch()
        
        refresh_btn = QPushButton("🔄")
        refresh_btn.setFixedSize(30, 30)
        refresh_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        refresh_btn.setStyleSheet(f"""
            QPushButton {{
                background: transparent;
                border: none;
                color: {COLORS['text_secondary']};
                font-size: 16px;
            }}
            QPushButton:hover {{
                background: rgba(255, 255, 255, 0.1);
                border-radius: 6px;
            }}
        """)
        refresh_btn.clicked.connect(self.manual_refresh)
        header_layout.addWidget(refresh_btn)
        
        settings_btn = QPushButton("⚙️")
        settings_btn.setFixedSize(30, 30)
        settings_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        settings_btn.setStyleSheet(f"""
            QPushButton {{
                background: transparent;
                border: none;
                color: {COLORS['text_secondary']};
                font-size: 16px;
            }}
            QPushButton:hover {{
                background: rgba(255, 255, 255, 0.1);
                border-radius: 6px;
            }}
        """)
        settings_btn.clicked.connect(self.show_settings)
        header_layout.addWidget(settings_btn)
        
        close_btn = QPushButton("×")
        close_btn.setFixedSize(30, 30)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(f"""
            QPushButton {{
                background: transparent;
                border: none;
                color: {COLORS['text_secondary']};
                font-size: 20px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: rgba(239, 68, 68, 0.8);
                border-radius: 6px;
                color: white;
            }}
        """)
        close_btn.clicked.connect(self.close_overlay)
        header_layout.addWidget(close_btn)
        
        main_layout.addLayout(header_layout)
        
        # Matches stacked directly — no scroll area
        self.matches_layout = QVBoxLayout()
        self.matches_layout.setSpacing(8)
        self.matches_layout.setAlignment(Qt.AlignmentFlag.AlignTop)
        main_layout.addLayout(self.matches_layout)
        
        self.status_label = QLabel("Loading...")
        self.status_label.setStyleSheet(f"color: {COLORS['text_secondary']}; font-size: 11px;")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        main_layout.addWidget(self.status_label)
    
    def load_followed_teams(self):
        try:
            if SCORES_DATA_FILE.exists():
                with open(SCORES_DATA_FILE, 'r') as f:
                    data = json.load(f)
                    return data.get('followed_teams', [])
        except:
            pass
        return []
    
    def save_followed_teams(self):
        try:
            with open(SCORES_DATA_FILE, 'w') as f:
                json.dump({'followed_teams': self.followed_teams}, f, indent=4)
        except:
            pass
    
    def start_updates(self):
        if self.followed_teams:
            team_data = [(str(t['id']), t['name']) for t in self.followed_teams]
            
            self.update_scores()
            
            # Start update thread
            self.update_thread = LiveScoreUpdateThread(self.api_client, team_data)
            self.update_thread.scores_updated.connect(self.on_scores_updated)
            self.update_thread.finished.connect(self.on_thread_finished)
            self.update_thread.start()
        else:
            self.status_label.setText("No teams followed. Click ⚙️")
            self.setFixedSize(390, 120)
            self.resized.emit()
    
    def manual_refresh(self):
        if not self.is_closing:
            self.status_label.setText("Refreshing...")
            self.update_scores()
    
    def update_scores(self):
        if self.followed_teams and not self.is_closing:
            team_data = [(str(t['id']), t['name']) for t in self.followed_teams]
            matches = self.api_client.get_live_matches(team_data)
            self.on_scores_updated(matches)
    
    def on_scores_updated(self, matches):
        if self.is_closing:
            return
        
        for widget in self.score_widgets:
            widget.setParent(None)
            widget.deleteLater()
        self.score_widgets.clear()
        
        if matches:
            self.status_label.setText(f"Updated: {datetime.now().strftime('%H:%M')}")
            for match in matches:
                widget = LiveScoreWidget(match)
                widget.setFixedWidth(360)
                self.matches_layout.addWidget(widget)
                self.score_widgets.append(widget)
        else:
            self.status_label.setText("No matches found")
        
        # Calculate exact height from content
        header_h = 55   # header row
        status_h = 25   # status label
        padding  = 30   # margins + spacing
        items_h  = len(self.score_widgets) * (80 + 8)  # widget height + spacing
        total_h  = header_h + status_h + padding + items_h
        
        self.setFixedSize(390, max(120, total_h))
        self.resized.emit()
    
    def show_settings(self):
        if self.is_closing:
            return
        
        try:
            dialog = TeamManagementDialog(self.api_client, self.followed_teams, self)
            dialog.teams_updated.connect(self.on_teams_updated)
            
            if self.parent():
                center = self.parent().rect().center()
                dialog.move(center.x() - 250, center.y() - 300)
            
            dialog.show()
        except:
            pass
    
    def on_teams_updated(self, teams):
        if self.is_closing:
            return
        
        self.followed_teams = teams
        self.save_followed_teams()
        
        # Stop old thread
        if self.update_thread:
            self.update_thread.stop()
            self.update_thread = None
        
        # Start new
        self.start_updates()
    
    def on_thread_finished(self):
        """Thread has finished"""
        print("⚽ Update thread finished")
    
    def close_overlay(self):
        """Close overlay with complete cleanup"""
        if self.is_closing:
            return
        
        print("⚽ Closing overlay...")
        self.is_closing = True
        
        # Disconnect all signals first
        try:
            if self.update_thread:
                self.update_thread.scores_updated.disconnect()
                self.update_thread.finished.disconnect()
        except:
            pass
        
        # Stop thread
        if self.update_thread:
            print("⚽ Stopping update thread...")
            self.update_thread.stop()
            # Don't wait - just let it finish naturally
            self.update_thread = None
        
        # Clear widgets immediately
        for widget in self.score_widgets:
            widget.setParent(None)
            widget.deleteLater()
        self.score_widgets.clear()
        
        # Hide first
        self.hide()
        
        # Emit closed signal
        try:
            self.closed.emit()
        except:
            pass
        
        # Schedule deletion
        QTimer.singleShot(100, self.deleteLater)
        
        print("⚽ Overlay closed")


class TeamManagementDialog(QFrame):
    teams_updated = pyqtSignal(list)
    
    def __init__(self, api_client, followed_teams, parent=None):
        super().__init__(parent)
        self.api_client = api_client
        self.followed_teams = followed_teams.copy()
        
        self.setWindowTitle("Manage Teams")
        self.setFixedSize(500, 600)
        self.setWindowFlags(Qt.WindowType.Window)
        
        self.setStyleSheet(f"""
            QFrame {{
                background: {COLORS['bg_primary']};
                border: 2px solid {COLORS['border']};
                border-radius: 12px;
            }}
        """)
        
        self.setup_ui()
    
    def setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(15)
        
        title = QLabel("⚽ Manage Teams")
        title.setStyleSheet(f"color: {COLORS['text_primary']}; font-size: 18px; font-weight: bold;")
        layout.addWidget(title)
        
        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText("Search teams...")
        self.search_input.setStyleSheet(f"""
            QLineEdit {{
                background: {COLORS['bg_tertiary']};
                color: {COLORS['text_primary']};
                border: 2px solid {COLORS['border']};
                border-radius: 8px;
                padding: 8px;
            }}
        """)
        self.search_input.textChanged.connect(self.search_teams)
        layout.addWidget(self.search_input)
        
        self.search_results = QListWidget()
        self.search_results.setMaximumHeight(200)
        self.search_results.setStyleSheet(f"""
            QListWidget {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
            }}
            QListWidget::item {{ padding: 8px; }}
            QListWidget::item:hover {{ background: {COLORS['accent_primary']}; }}
        """)
        self.search_results.itemClicked.connect(self.add_team)
        self.search_results.hide()
        layout.addWidget(self.search_results)
        
        layout.addWidget(QLabel("Followed Teams:", styleSheet=f"color: {COLORS['text_primary']}; font-weight: bold;"))
        
        self.followed_list = QListWidget()
        self.followed_list.setStyleSheet(f"""
            QListWidget {{
                background: {COLORS['bg_secondary']};
                color: {COLORS['text_primary']};
                border: 1px solid {COLORS['border']};
                border-radius: 8px;
            }}
            QListWidget::item {{ padding: 10px; }}
        """)
        self.refresh_list()
        layout.addWidget(self.followed_list)
        
        btn_layout = QHBoxLayout()
        
        remove_btn = QPushButton("Remove")
        remove_btn.setFixedHeight(40)
        remove_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        remove_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['error']};
                color: white;
                border: none;
                border-radius: 8px;
                font-weight: bold;
            }}
        """)
        remove_btn.clicked.connect(self.remove_team)
        btn_layout.addWidget(remove_btn)
        
        btn_layout.addStretch()
        
        save_btn = QPushButton("Save")
        save_btn.setFixedHeight(40)
        save_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        save_btn.setStyleSheet(f"""
            QPushButton {{
                background: {COLORS['accent_primary']};
                color: white;
                border: none;
                border-radius: 8px;
                font-weight: bold;
                padding: 0 30px;
            }}
        """)
        save_btn.clicked.connect(self.save)
        btn_layout.addWidget(save_btn)
        
        layout.addLayout(btn_layout)
    
    def search_teams(self, query):
        if len(query) < 2:
            self.search_results.hide()
            return
        
        teams = self.api_client.search_teams(query)
        self.search_results.clear()
        
        if teams:
            for team in teams:
                item = QListWidgetItem(f"{team['name']} ({team['country']}) - {team['league']}")
                item.setData(Qt.ItemDataRole.UserRole, team)
                self.search_results.addItem(item)
            self.search_results.show()
        else:
            self.search_results.hide()
    
    def add_team(self, item):
        team = item.data(Qt.ItemDataRole.UserRole)
        if not any(t['id'] == team['id'] for t in self.followed_teams):
            self.followed_teams.append(team)
            self.refresh_list()
            self.search_input.clear()
            self.search_results.hide()
    
    def refresh_list(self):
        self.followed_list.clear()
        for team in self.followed_teams:
            item = QListWidgetItem(f"{team['name']} - {team.get('league', team.get('country'))}")
            item.setData(Qt.ItemDataRole.UserRole, team)
            self.followed_list.addItem(item)
    
    def remove_team(self):
        item = self.followed_list.currentItem()
        if item:
            team = item.data(Qt.ItemDataRole.UserRole)
            self.followed_teams = [t for t in self.followed_teams if t['id'] != team['id']]
            self.refresh_list()
    
    def save(self):
        try:
            self.teams_updated.emit(self.followed_teams)
        except:
            pass
        self.close()


def create_live_score_overlay(parent=None):
    return LiveScoreOverlay(parent)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    overlay = create_live_score_overlay()
    overlay.move(100, 100)
    overlay.show()
    sys.exit(app.exec())
