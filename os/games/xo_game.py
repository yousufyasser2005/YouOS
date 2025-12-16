import tkinter as tk
from tkinter import messagebox

def open_tic_tac_toe(root):
    win = tk.Toplevel(root)
    win.title("Tic Tac Toe")
    win.geometry("300x350")

    current_player = ['X']

    buttons = [[None for _ in range(3)] for _ in range(3)]

    def check_winner():
        for i in range(3):
            if buttons[i][0]['text'] == buttons[i][1]['text'] == buttons[i][2]['text'] != "":
                return True
            if buttons[0][i]['text'] == buttons[1][i]['text'] == buttons[2][i]['text'] != "":
                return True

        if buttons[0][0]['text'] == buttons[1][1]['text'] == buttons[2][2]['text'] != "":
            return True
        if buttons[0][2]['text'] == buttons[1][1]['text'] == buttons[2][0]['text'] != "":
            return True

        return False

    def is_draw():
        for row in buttons:
            for btn in row:
                if btn['text'] == "":
                    return False
        return True

    def click(r, c):
        if buttons[r][c]['text'] == "":
            buttons[r][c]['text'] = current_player[0]
            if check_winner():
                messagebox.showinfo("Winner", f"Player {current_player[0]} wins!")
                reset()
            elif is_draw():
                messagebox.showinfo("Draw", "It's a draw!")
                reset()
            else:
                current_player[0] = 'O' if current_player[0] == 'X' else 'X'

    def reset():
        for row in buttons:
            for btn in row:
                btn['text'] = ""
        current_player[0] = 'X'

    frame = tk.Frame(win)
    frame.pack(pady=10)

    for i in range(3):
        for j in range(3):
            btn = tk.Button(frame, text="", font=("Arial", 20), width=5, height=2, command=lambda r=i, c=j: click(r, c))
            btn.grid(row=i, column=j)
            buttons[i][j] = btn

    tk.Button(win, text="Reset", command=reset).pack(pady=10)

    return win
