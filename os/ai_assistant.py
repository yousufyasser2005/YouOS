"""
YouOS 10 - AI Assistant
ai_assistant.py - Integrated AI chatbot with system control capabilities
"""

import sys
from pathlib import Path
from PyQt6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel, 
                              QPushButton, QLineEdit, QScrollArea, QFrame,
                              QGraphicsDropShadowEffect, QApplication, QGraphicsBlurEffect)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QPropertyAnimation, QEasingCurve, QPoint
from PyQt6.QtGui import QColor, QPainter, QPainterPath, QLinearGradient, QFont

# Import system control utilities
try:
    from utils import get_volume, set_volume, get_brightness, set_brightness, play_sound
except ImportError:
    def get_volume():
        return 50
    def set_volume(val):
        pass
    def get_brightness():
        return 75
    def set_brightness(val):
        pass
    def play_sound(name):
        pass

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


class MessageBubble(QFrame):
    """Glassmorphic message bubble for chat"""
    
    def __init__(self, message, is_user=True, parent=None):
        super().__init__(parent)
        self.message = message
        self.is_user = is_user
        self.setup_ui()
    
    def setup_ui(self):
        """Setup message bubble UI"""
        layout = QVBoxLayout(self)
        layout.setContentsMargins(20, 15, 20, 15)
        layout.setSpacing(5)
        
        # Message text
        message_label = QLabel(self.message)
        message_label.setWordWrap(True)
        message_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        
        if self.is_user:
            message_label.setStyleSheet(f"""
                color: {COLORS['text_primary']};
                font-size: 14px;
                background: transparent;
            """)
            self.setStyleSheet(f"""
                QFrame {{
                    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                        stop:0 rgba(59, 130, 246, 0.3),
                        stop:1 rgba(96, 165, 250, 0.2));
                    border: 1px solid rgba(59, 130, 246, 0.4);
                    border-radius: 18px;
                }}
            """)
        else:
            message_label.setStyleSheet(f"""
                color: {COLORS['text_primary']};
                font-size: 14px;
                background: transparent;
            """)
            self.setStyleSheet(f"""
                QFrame {{
                    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                        stop:0 rgba(37, 37, 56, 0.6),
                        stop:1 rgba(26, 26, 46, 0.4));
                    border: 1px solid rgba(255, 255, 255, 0.15);
                    border-radius: 18px;
                }}
            """)
        
        layout.addWidget(message_label)
        
        # Add shadow effect
        shadow = QGraphicsDropShadowEffect()
        shadow.setBlurRadius(15)
        shadow.setColor(QColor(0, 0, 0, 60))
        shadow.setOffset(0, 4)
        self.setGraphicsEffect(shadow)
        
        # Set maximum width
        self.setMaximumWidth(600)
        
        # Entrance animation
        self.setWindowOpacity(0.0)
        self.fade_in()
    
    def fade_in(self):
        """Fade in animation"""
        self.anim = QPropertyAnimation(self, b"windowOpacity")
        self.anim.setDuration(300)
        self.anim.setStartValue(0.0)
        self.anim.setEndValue(1.0)
        self.anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.anim.start()


class TypingIndicator(QWidget):
    """Animated typing indicator"""
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(60, 30)
        self.dot_positions = [0, 0, 0]
        self.animation_step = 0
        
        # Start animation
        self.timer = QTimer()
        self.timer.timeout.connect(self.animate)
        self.timer.start(100)
    
    def animate(self):
        """Animate dots"""
        self.animation_step = (self.animation_step + 1) % 12
        
        for i in range(3):
            offset = (self.animation_step - i * 3) % 12
            if offset < 6:
                self.dot_positions[i] = -offset * 2
            else:
                self.dot_positions[i] = (offset - 6) * 2
        
        self.update()
    
    def paintEvent(self, event):
        """Paint dots"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Draw three dots
        dot_spacing = 15
        start_x = 10
        y = 15
        
        for i, offset in enumerate(self.dot_positions):
            x = start_x + i * dot_spacing
            
            # Dot with glow
            painter.setBrush(QColor(59, 130, 246, 100))
            painter.setPen(Qt.PenStyle.NoPen)
            painter.drawEllipse(x - 4, y + offset - 4, 12, 12)
            
            painter.setBrush(QColor(59, 130, 246))
            painter.drawEllipse(x - 2, y + offset - 2, 8, 8)


class AIAssistantWindow(QWidget):
    """Full-screen AI assistant chat window"""
    
    closed = pyqtSignal()
    
    def __init__(self, desktop_manager, parent=None):
        super().__init__(parent)
        self.desktop_manager = desktop_manager
        self.chat_history = []
        
        # AI personality - SET BEFORE setup_ui()
        self.assistant_name = "YouOS Assistant"
        self.greeting = "Hello! 👋 I'm your YouOS assistant, and I'm here to make your life easier! I can help you control system settings, answer questions, and even have a friendly chat. Try asking me to adjust volume or brightness, or just say hi! Type 'exit' to close me anytime. 😊"
        
        self.setup_ui()
        self.setup_animations()
        
        # Add greeting message
        QTimer.singleShot(500, self.show_greeting)
    
    def setup_ui(self):
        """Setup full-screen chat UI"""
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        
        # Get screen geometry
        screen = QApplication.primaryScreen().geometry()
        self.setGeometry(screen)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        
        # Chat area (no header anymore)
        self.chat_scroll = QScrollArea()
        self.chat_scroll.setWidgetResizable(True)
        self.chat_scroll.setStyleSheet("""
            QScrollArea {
                background: transparent;
                border: none;
            }
            QScrollBar:vertical {
                background: rgba(255, 255, 255, 0.1);
                width: 10px;
                border-radius: 5px;
                margin: 5px;
            }
            QScrollBar::handle:vertical {
                background: rgba(59, 130, 246, 0.5);
                border-radius: 5px;
                min-height: 30px;
            }
            QScrollBar::handle:vertical:hover {
                background: rgba(59, 130, 246, 0.7);
            }
        """)
        
        self.chat_container = QWidget()
        self.chat_container.setStyleSheet("background: transparent;")
        self.chat_layout = QVBoxLayout(self.chat_container)
        self.chat_layout.setContentsMargins(50, 30, 50, 30)
        self.chat_layout.setSpacing(20)
        # Add stretch at the top so messages stack from bottom
        self.chat_layout.addStretch()
        
        self.chat_scroll.setWidget(self.chat_container)
        layout.addWidget(self.chat_scroll)
        
        # Input area
        input_container = QWidget()
        input_container.setFixedHeight(120)
        input_container.setStyleSheet("""
            QWidget {
                background: rgba(26, 26, 46, 0.7);
                border-top: 1px solid rgba(255, 255, 255, 0.15);
            }
        """)
        
        input_layout = QHBoxLayout(input_container)
        input_layout.setContentsMargins(50, 20, 50, 20)
        input_layout.setSpacing(15)
        
        # Text input
        self.input_field = QLineEdit()
        self.input_field.setPlaceholderText("Ask me anything or give me a command...")
        self.input_field.setStyleSheet(f"""
            QLineEdit {{
                background: rgba(37, 37, 56, 0.8);
                color: {COLORS['text_primary']};
                border: 2px solid rgba(255, 255, 255, 0.1);
                border-radius: 25px;
                padding: 15px 25px;
                font-size: 15px;
            }}
            QLineEdit:focus {{
                border-color: {COLORS['accent_primary']};
                background: rgba(37, 37, 56, 0.95);
            }}
        """)
        self.input_field.returnPressed.connect(self.send_message)
        input_layout.addWidget(self.input_field)
        
        # Send button
        self.send_btn = QPushButton("Send")
        self.send_btn.setFixedSize(120, 50)
        self.send_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self.send_btn.setStyleSheet(f"""
            QPushButton {{
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 {COLORS['accent_primary']},
                    stop:1 {COLORS['accent_hover']});
                color: white;
                border: none;
                border-radius: 25px;
                font-size: 15px;
                font-weight: bold;
            }}
            QPushButton:hover {{
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 {COLORS['accent_hover']},
                    stop:1 #93c5fd);
            }}
            QPushButton:pressed {{
                background: {COLORS['accent_primary']};
            }}
        """)
        self.send_btn.clicked.connect(self.send_message)
        input_layout.addWidget(self.send_btn)
        
        layout.addWidget(input_container)
    
    def setup_animations(self):
        """Setup entrance animations"""
        self.setWindowOpacity(0.0)
        
        self.fade_anim = QPropertyAnimation(self, b"windowOpacity")
        self.fade_anim.setDuration(300)
        self.fade_anim.setStartValue(0.0)
        self.fade_anim.setEndValue(1.0)
        self.fade_anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        self.fade_anim.start()
        
        play_sound("click.wav")
    
    def paintEvent(self, event):
        """Paint blurred glass background"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Light transparent overlay instead of dark background
        painter.fillRect(self.rect(), QColor(15, 15, 30, 100))
        
        # Add subtle gradient overlay for depth
        gradient = QLinearGradient(0, 0, 0, self.height())
        gradient.setColorAt(0, QColor(26, 26, 46, 60))
        gradient.setColorAt(0.5, QColor(15, 15, 30, 30))
        gradient.setColorAt(1, QColor(26, 26, 46, 80))
        painter.fillRect(self.rect(), gradient)
    
    def show_greeting(self):
        """Show initial greeting message"""
        self.add_message(self.greeting, is_user=False)
    
    def send_message(self):
        """Send user message"""
        message = self.input_field.text().strip()
        if not message:
            return
        
        # Clear input
        self.input_field.clear()
        
        # Add user message
        self.add_message(message, is_user=True)
        
        # Show typing indicator
        self.show_typing_indicator()
        
        # Process message after delay
        QTimer.singleShot(1000, lambda: self.process_message(message))
    
    def add_message(self, message, is_user=True):
        """Add message to chat"""
        # Create message bubble
        bubble = MessageBubble(message, is_user)
        
        # Create container for alignment
        container = QWidget()
        container_layout = QHBoxLayout(container)
        container_layout.setContentsMargins(0, 0, 0, 0)
        
        if is_user:
            container_layout.addStretch()
            container_layout.addWidget(bubble)
        else:
            container_layout.addWidget(bubble)
            container_layout.addStretch()
        
        # Insert before stretch
        self.chat_layout.insertWidget(self.chat_layout.count() - 1, container)
        
        # Scroll to bottom
        QTimer.singleShot(100, self.scroll_to_bottom)
        
        # Store in history
        self.chat_history.append({'message': message, 'is_user': is_user})
    
    def show_typing_indicator(self):
        """Show typing indicator"""
        self.typing_widget = QWidget()
        typing_layout = QHBoxLayout(self.typing_widget)
        typing_layout.setContentsMargins(0, 0, 0, 0)
        
        indicator = TypingIndicator()
        typing_layout.addWidget(indicator)
        typing_layout.addStretch()
        
        self.chat_layout.insertWidget(self.chat_layout.count() - 1, self.typing_widget)
        self.scroll_to_bottom()
    
    def remove_typing_indicator(self):
        """Remove typing indicator"""
        if hasattr(self, 'typing_widget'):
            self.chat_layout.removeWidget(self.typing_widget)
            self.typing_widget.deleteLater()
            del self.typing_widget
    
    def process_message(self, message):
        """Process user message and generate response"""
        self.remove_typing_indicator()
        
        message_lower = message.lower()
        response = ""
        
        # Volume control
        if any(word in message_lower for word in ['volume', 'sound', 'audio']):
            response = self.handle_volume_command(message_lower)
        
        # Brightness control
        elif any(word in message_lower for word in ['brightness', 'screen', 'display']):
            response = self.handle_brightness_command(message_lower)
        
        # System info
        elif any(word in message_lower for word in ['time', 'date', 'what time']):
            from datetime import datetime
            now = datetime.now()
            response = f"It's currently {now.strftime('%I:%M %p')} on {now.strftime('%B %d, %Y')}."
        
        # Battery info
        elif 'battery' in message_lower:
            response = self.get_battery_info()
        
        # Help
        elif any(word in message_lower for word in ['help', 'what can you do', 'commands', 'capabilities']):
            response = """I can help you with:
• Volume control - "set volume to 50" or "increase volume"
• Brightness control - "set brightness to 80" or "dim screen"
• System info - ask about time, date, or battery
• General chat - I'm here to help!
• Type 'exit' or 'bye' to close this assistant"""
        
        # Greetings
        elif any(word in message_lower for word in ['hello', 'hi', 'hey', 'greetings', 'good morning', 'good afternoon', 'good evening']):
            import random
            greetings = [
                "Hello! How can I assist you today? 😊",
                "Hi there! What can I help you with?",
                "Hey! I'm here to help. What do you need?",
                "Greetings! How may I be of service?",
                "Hi! Great to see you! What can I do for you?",
                "Hello! Ready to help you out. What's up?"
            ]
            response = random.choice(greetings)
        
        # Thanks / Appreciation
        elif any(word in message_lower for word in ['thank', 'thanks', 'appreciate', 'awesome', 'great job', 'good job', 'well done', 'perfect', 'excellent', 'nice', 'cool']):
            import random
            responses = [
                "You're welcome! Happy to help! 😊",
                "My pleasure! Let me know if you need anything else.",
                "Glad I could help!",
                "Anytime! I'm here if you need me.",
                "Thank you! That means a lot! 🎉",
                "I appreciate that! Always here to assist!",
                "You're too kind! Let me know what else I can do.",
                "Thanks! I'm happy I could be useful! 😄",
                "Awesome! Feel free to ask me anything else!",
                "Glad it worked out! I'm here whenever you need me."
            ]
            response = random.choice(responses)
        
        # How are you
        elif any(phrase in message_lower for phrase in ['how are you', 'how are u', 'how r you', 'how r u', "how's it going", 'whats up', "what's up"]):
            import random
            responses = [
                "I'm doing great! Thanks for asking! How can I help you today?",
                "I'm excellent! Ready to assist you with anything you need!",
                "Doing wonderfully! What can I do for you?",
                "I'm fantastic! Just waiting to help you out! 😊",
                "All systems running smoothly! How about you? Need any help?"
            ]
            response = random.choice(responses)
        
        # Goodbye / Exit commands
        elif any(word in message_lower for word in ['bye', 'goodbye', 'see you', 'later', 'gotta go', 'gtg', 'exit', 'quit', 'close']):
            import random
            responses = [
                "Goodbye! Feel free to come back anytime! 👋",
                "See you later! I'm here whenever you need me!",
                "Bye! Take care and see you soon!",
                "Catch you later! Don't hesitate to reach out! 😊",
                "Until next time! Have a great day!"
            ]
            response = random.choice(responses)
            self.add_message(response, is_user=False)
            play_sound("ding.wav")
            # Close assistant after showing goodbye message
            QTimer.singleShot(1500, self.close_assistant)
            return  # Return early to avoid adding message twice
        
        # Compliments to AI
        elif any(word in message_lower for word in ['smart', 'clever', 'helpful', 'useful', 'amazing', 'love you', 'you rock', 'youre the best', 'best assistant']):
            import random
            responses = [
                "Aww, thank you so much! You're pretty amazing yourself! 💙",
                "That's so kind of you to say! I try my best! 😊",
                "You're making me blush! Happy to be of service!",
                "Thanks! I really appreciate that! You rock too! 🌟",
                "You're too kind! I'm just doing what I love - helping you!",
                "That means a lot! I'm here to make your day easier! 😄"
            ]
            response = random.choice(responses)
        
        # Jokes / Fun
        elif any(word in message_lower for word in ['joke', 'funny', 'laugh', 'entertain']):
            import random
            jokes = [
                "Why did the computer go to the doctor? Because it had a virus! 😄",
                "What do you call a computer that sings? A-Dell! 🎵",
                "Why was the computer cold? It left its Windows open! 🪟",
                "How does a computer get drunk? It takes screenshots! 📸",
                "What's a computer's favorite snack? Microchips! 🍟"
            ]
            response = random.choice(jokes)
        
        # Sad/Problem expressions
        elif any(word in message_lower for word in ['sad', 'upset', 'angry', 'frustrated', 'annoyed', 'problem', 'issue', 'not working', 'broken']):
            import random
            responses = [
                "I'm sorry to hear that. Let me help you fix it! What's the issue?",
                "Oh no! I'm here to help. Tell me what's wrong and we'll solve it together!",
                "I understand your frustration. Let's work on this together! What can I do?",
                "Don't worry, I'm here to help! What's bothering you?",
                "I'm sorry you're having trouble. Let me see what I can do to help! 💪"
            ]
            response = random.choice(responses)
        
        # Bored
        elif any(word in message_lower for word in ['bored', 'boring', 'nothing to do']):
            import random
            responses = [
                "Let's change that! Want to try adjusting some settings or exploring what I can do? Type 'help' for ideas!",
                "How about we have some fun? I can tell you a joke, or help you customize your system!",
                "Boredom detected! Let's make things interesting. What would you like to do?",
                "Time to liven things up! Want to see what cool things I can help you with?"
            ]
            response = random.choice(responses)
        
        # Who are you
        elif any(phrase in message_lower for phrase in ['who are you', 'what are you', 'your name', 'introduce yourself']):
            response = "I'm your YouOS Assistant! 🤖 I'm here to help you control your system, answer questions, and make your computing experience smoother. I can adjust volume, brightness, check system info, and chat with you. What would you like to know?"
        
        # Good/Affirmations
        elif any(word in message_lower for word in ['good', 'ok', 'okay', 'alright', 'fine', 'sure']):
            import random
            responses = [
                "Great! Anything else I can help you with?",
                "Awesome! Let me know if you need anything else!",
                "Perfect! I'm here if you need me!",
                "Cool! Just ask if you need more help!",
                "Sounds good! Feel free to reach out anytime! 😊"
            ]
            response = random.choice(responses)
        
        # Yes
        elif message_lower in ['yes', 'yeah', 'yep', 'yup', 'sure', 'of course', 'definitely']:
            import random
            responses = [
                "Excellent! What would you like to do?",
                "Great! How can I assist you?",
                "Perfect! What do you need help with?",
                "Wonderful! What's next?"
            ]
            response = random.choice(responses)
        
        # No
        elif message_lower in ['no', 'nope', 'nah', 'not really']:
            import random
            responses = [
                "No problem! Let me know if you change your mind!",
                "Alright! I'm here whenever you need me!",
                "Okay! Feel free to ask me anything else!",
                "Got it! Just reach out if you need help!"
            ]
            response = random.choice(responses)
        
        # Default response
        else:
            import random
            default_responses = [
                "I'm not sure how to help with that yet. Try asking me to adjust volume or brightness, or type 'help' to see what I can do!",
                "Hmm, I didn't quite catch that. Could you try rephrasing? Or type 'help' for ideas!",
                "I'm still learning! For now, I can help with volume, brightness, and system info. Type 'help' for more!",
                "I don't understand that command yet, but I'm learning! Try 'help' to see what I can currently do."
            ]
            response = random.choice(default_responses)
        
        # Add response
        self.add_message(response, is_user=False)
        play_sound("ding.wav")
    
    def handle_volume_command(self, message):
        """Handle volume control commands"""
        current_volume = get_volume()
        
        if 'mute' in message:
            set_volume(0)
            return "🔇 Volume muted. Peace and quiet!"
        
        elif 'unmute' in message or 'restore' in message:
            set_volume(50)
            return "🔊 Volume restored to 50%. Welcome back to the sound!"
        
        elif any(word in message for word in ['increase', 'up', 'raise', 'louder', 'turn up', 'boost']):
            new_volume = min(100, current_volume + 10)
            set_volume(new_volume)
            if new_volume == 100:
                return f"🔊 Volume cranked up to {new_volume}%! That's max volume!"
            return f"🔊 Volume increased to {new_volume}%. Sounding good!"
        
        elif any(word in message for word in ['decrease', 'down', 'lower', 'quieter', 'turn down', 'reduce']):
            new_volume = max(0, current_volume - 10)
            set_volume(new_volume)
            if new_volume == 0:
                return f"🔇 Volume is now at {new_volume}%. Complete silence!"
            return f"🔉 Volume decreased to {new_volume}%. Nice and quiet!"
        
        elif 'set' in message or 'to' in message or 'at' in message:
            # Extract number
            import re
            numbers = re.findall(r'\d+', message)
            if numbers:
                volume = min(100, max(0, int(numbers[0])))
                set_volume(volume)
                if volume == 0:
                    return f"🔇 Volume set to {volume}%. Silence is golden!"
                elif volume < 30:
                    return f"🔈 Volume set to {volume}%. Nice and quiet!"
                elif volume < 70:
                    return f"🔉 Volume set to {volume}%. Perfect balance!"
                else:
                    return f"🔊 Volume set to {volume}%. That's loud and clear!"
        
        return f"🔊 Current volume is {current_volume}%. You can say 'increase volume', 'decrease volume', or 'set volume to 50'."
    
    def handle_brightness_command(self, message):
        """Handle brightness control commands"""
        current_brightness = get_brightness()
        
        if any(word in message for word in ['increase', 'up', 'raise', 'brighter', 'brighten']):
            new_brightness = min(100, current_brightness + 10)
            set_brightness(new_brightness)
            if new_brightness == 100:
                return f"☀️ Brightness maxed out at {new_brightness}%! Super bright!"
            return f"☀️ Brightness increased to {new_brightness}%. Looking brighter!"
        
        elif any(word in message for word in ['decrease', 'down', 'lower', 'dim', 'darker', 'darken']):
            new_brightness = max(10, current_brightness - 10)
            set_brightness(new_brightness)
            if new_brightness == 10:
                return f"🌙 Brightness lowered to {new_brightness}%. That's minimum brightness!"
            return f"🌙 Brightness decreased to {new_brightness}%. Easy on the eyes!"
        
        elif 'set' in message or 'to' in message or 'at' in message:
            # Extract number
            import re
            numbers = re.findall(r'\d+', message)
            if numbers:
                brightness = min(100, max(10, int(numbers[0])))
                set_brightness(brightness)
                if brightness < 30:
                    return f"🌙 Brightness set to {brightness}%. Nice and dim!"
                elif brightness < 70:
                    return f"🌤️ Brightness set to {brightness}%. Perfectly balanced!"
                else:
                    return f"☀️ Brightness set to {brightness}%. Bright and clear!"
        
        return f"💡 Current brightness is {current_brightness}%. You can say 'increase brightness', 'decrease brightness', or 'set brightness to 80'."
    
    def get_battery_info(self):
        """Get battery information"""
        try:
            from utils import get_battery_info
            battery = get_battery_info()
            percent = int(battery.get('percent', 0))
            plugged = battery.get('plugged', False)
            
            status = "charging ⚡" if plugged else "on battery 🔋"
            
            if plugged:
                if percent == 100:
                    return f"🔋 Battery is fully charged at {percent}%! You're all set!"
                elif percent > 80:
                    return f"⚡ Battery is at {percent}% and charging. Almost there!"
                else:
                    return f"⚡ Battery is at {percent}% and charging nicely!"
            else:
                if percent > 80:
                    return f"🔋 Battery is at {percent}% ({status}). Plenty of juice!"
                elif percent > 50:
                    return f"🔋 Battery is at {percent}% ({status}). Looking good!"
                elif percent > 20:
                    return f"🔋 Battery is at {percent}% ({status}). Consider charging soon!"
                else:
                    return f"🪫 Battery is at {percent}% ({status}). Time to plug in!"
        except:
            return "❌ Unable to get battery information. Sorry about that!"
    
    def scroll_to_bottom(self):
        """Scroll chat to bottom"""
        scrollbar = self.chat_scroll.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
    
    def close_assistant(self):
        """Close assistant with animation"""
        self.fade_out_anim = QPropertyAnimation(self, b"windowOpacity")
        self.fade_out_anim.setDuration(250)
        self.fade_out_anim.setStartValue(1.0)
        self.fade_out_anim.setEndValue(0.0)
        self.fade_out_anim.setEasingCurve(QEasingCurve.Type.InCubic)
        self.fade_out_anim.finished.connect(self.closed.emit)
        self.fade_out_anim.start()
        
        play_sound("click.wav")
    
    def keyPressEvent(self, event):
        """Handle escape key to close (alternative to 'exit' command)"""
        if event.key() == Qt.Key.Key_Escape:
            self.close_assistant()
        super().keyPressEvent(event)


if __name__ == "__main__":
    # Test the assistant
    app = QApplication(sys.argv)
    
    class MockDesktop:
        pass
    
    window = AIAssistantWindow(MockDesktop())
    window.show()
    sys.exit(app.exec())
