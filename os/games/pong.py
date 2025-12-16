import pygame
import sys
import random

# Initialize Pygame
pygame.init()

# Constants
SCREEN_WIDTH, SCREEN_HEIGHT = 800, 600
PADDLE_WIDTH, PADDLE_HEIGHT = 10, 100
BALL_SIZE = 15
PADDLE_SPEED = 6
BALL_SPEED = 5
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
FONT_SIZE = 32

# Overall stats
matches_played = 0
player_matches_won = 0
ai_matches_won = 0
total_player_points = 0
total_ai_points = 0

# Screen setup
screen = pygame.display.set_mode((SCREEN_WIDTH, SCREEN_HEIGHT))
pygame.display.set_caption('Pong')
clock = pygame.time.Clock()
font = pygame.font.Font(None, FONT_SIZE)
large_font = pygame.font.Font(None, 64)

# Sounds (optional: download .wav files)
try:
    bounce_sound = pygame.mixer.Sound('bounce.wav')
    score_sound = pygame.mixer.Sound('score.wav')
except FileNotFoundError:
    bounce_sound = score_sound = None  # No sounds if files missing

# Classes
class Paddle:
    def __init__(self, x, y):
        self.rect = pygame.Rect(x, y, PADDLE_WIDTH, PADDLE_HEIGHT)
    
    def move(self, dy):
        self.rect.y += dy
        self.rect.y = max(0, min(SCREEN_HEIGHT - PADDLE_HEIGHT, self.rect.y))
    
    def draw(self):
        pygame.draw.rect(screen, WHITE, self.rect)

class Ball:
    def __init__(self):
        self.rect = pygame.Rect(SCREEN_WIDTH // 2, SCREEN_HEIGHT // 2, BALL_SIZE, BALL_SIZE)
        self.dx = random.choice([-BALL_SPEED, BALL_SPEED])
        self.dy = random.randint(-BALL_SPEED, BALL_SPEED)
    
    def move(self):
        self.rect.x += self.dx
        self.rect.y += self.dy
    
    def bounce_wall(self):
        if self.rect.top <= 0 or self.rect.bottom >= SCREEN_HEIGHT:
            self.dy = -self.dy
            if bounce_sound:
                bounce_sound.play()
    
    def bounce_paddle(self, paddle):
        if self.rect.colliderect(paddle.rect):
            self.dx = -self.dx
            self.dy += random.randint(-2, 2)  # Add some randomness
            if bounce_sound:
                bounce_sound.play()
            return True
        return False
    
    def reset(self):
        self.rect.center = (SCREEN_WIDTH // 2, SCREEN_HEIGHT // 2)
        self.dx = random.choice([-BALL_SPEED, BALL_SPEED])
        self.dy = random.randint(-BALL_SPEED, BALL_SPEED)
    
    def draw(self):
        pygame.draw.ellipse(screen, WHITE, self.rect)

# Game functions
def draw_text(text, font, color, x, y):
    text_surf = font.render(text, True, color)
    screen.blit(text_surf, (x, y))

def main_menu():
    while True:
        screen.fill(BLACK)
        draw_text("Pong", large_font, WHITE, SCREEN_WIDTH // 2 - 50, SCREEN_HEIGHT // 4)
        draw_text("Press S to Start", font, WHITE, SCREEN_WIDTH // 2 - 100, SCREEN_HEIGHT // 2)
        draw_text("Press Q to Quit", font, WHITE, SCREEN_WIDTH // 2 - 100, SCREEN_HEIGHT // 2 + 50)
        pygame.display.flip()
        
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                sys.exit()
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_s:
                    return  # Start game
                if event.key == pygame.K_q:
                    sys.exit()

def game_loop():
    player_paddle = Paddle(20, SCREEN_HEIGHT // 2 - PADDLE_HEIGHT // 2)
    ai_paddle = Paddle(SCREEN_WIDTH - 30, SCREEN_HEIGHT // 2 - PADDLE_HEIGHT // 2)
    ball = Ball()
    
    player_score = 0
    ai_score = 0
    game_over = False
    
    while not game_over:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                sys.exit()
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    return  # Back to menu
        
        # Player input
        keys = pygame.key.get_pressed()
        if keys[pygame.K_w]:
            player_paddle.move(-PADDLE_SPEED)
        if keys[pygame.K_s]:
            player_paddle.move(PADDLE_SPEED)
        
        # AI movement (simple: follow ball)
        if ai_paddle.rect.centery < ball.rect.centery:
            ai_paddle.move(PADDLE_SPEED // 2)  # Slower for fairness
        elif ai_paddle.rect.centery > ball.rect.centery:
            ai_paddle.move(-PADDLE_SPEED // 2)
        
        # Ball movement and collisions
        ball.move()
        ball.bounce_wall()
        ball.bounce_paddle(player_paddle)
        ball.bounce_paddle(ai_paddle)
        
        # Scoring
        if ball.rect.left <= 0:
            ai_score += 1
            if score_sound:
                score_sound.play()
            ball.reset()
        if ball.rect.right >= SCREEN_WIDTH:
            player_score += 1
            if score_sound:
                score_sound.play()
            ball.reset()
        
        # Check win condition (first to 10)
        if player_score >= 10 or ai_score >= 10:
            game_over = True
        
        # Draw everything
        screen.fill(BLACK)
        # Center line
        pygame.draw.line(screen, WHITE, (SCREEN_WIDTH // 2, 0), (SCREEN_WIDTH // 2, SCREEN_HEIGHT), 2)
        player_paddle.draw()
        ai_paddle.draw()
        ball.draw()
        draw_text(str(player_score), font, WHITE, SCREEN_WIDTH // 2 - 50, 20)
        draw_text(str(ai_score), font, WHITE, SCREEN_WIDTH // 2 + 30, 20)
        pygame.display.flip()
        clock.tick(60)
    
    # Update overall stats
    global matches_played, player_matches_won, ai_matches_won, total_player_points, total_ai_points
    matches_played += 1
    total_player_points += player_score
    total_ai_points += ai_score
    if player_score > ai_score:
        player_matches_won += 1
    else:
        ai_matches_won += 1
    
    # Game over screen
    winner = "Player" if player_score > ai_score else "AI"
    diff = total_player_points - total_ai_points
    screen.fill(BLACK)
    draw_text(f"{winner} Wins!", large_font, WHITE, SCREEN_WIDTH // 2 - 150, SCREEN_HEIGHT // 2 - 150)
    draw_text(f"Matches played: {matches_played}", font, WHITE, SCREEN_WIDTH // 2 - 100, SCREEN_HEIGHT // 2 - 100)
    draw_text(f"Matches won: Player {player_matches_won} - AI {ai_matches_won}", font, WHITE, SCREEN_WIDTH // 2 - 150, SCREEN_HEIGHT // 2 - 50)
    draw_text(f"Total points: Player {total_player_points} - AI {total_ai_points}", font, WHITE, SCREEN_WIDTH // 2 - 150, SCREEN_HEIGHT // 2)
    draw_text(f"Total score diff: {diff}", font, WHITE, SCREEN_WIDTH // 2 - 80, SCREEN_HEIGHT // 2 + 50)
    draw_text("Press R to Restart or ESC for Menu", font, WHITE, SCREEN_WIDTH // 2 - 200, SCREEN_HEIGHT // 2 + 100)
    pygame.display.flip()
    
    while True:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                sys.exit()
            if event.type == pygame.KEYDOWN:
                if event.key == pygame.K_r:
                    game_loop()  # Restart
                if event.key == pygame.K_ESCAPE:
                    return  # Back to menu

# Run the game
while True:
    main_menu()
    game_loop()