# calculator.py
import tkinter as tk

def open_calculator(root):
    calc_win = tk.Toplevel(root)
    calc_win.title("Calculator")
    calc_win.geometry("300x400")
    calc_win.resizable(False, False)

    expression = tk.StringVar()

    def press(num):
        expression.set(expression.get() + str(num))

    def equalpress():
        try:
            total = str(eval(expression.get()))
            expression.set(total)
        except:
            expression.set("Error")

    def clear():
        expression.set("")

    entry = tk.Entry(calc_win, textvariable=expression, font=('Arial', 20), bd=10, relief=tk.RIDGE, justify='right')
    entry.pack(fill='both', padx=10, pady=10)

    btn_frame = tk.Frame(calc_win)
    btn_frame.pack()

    buttons = [
        ('7', '8', '9', '/'),
        ('4', '5', '6', '*'),
        ('1', '2', '3', '-'),
        ('0', '.', '=', '+'),
    ]

    for row_vals in buttons:
        row = tk.Frame(btn_frame)
        row.pack(expand=True, fill='both')
        for val in row_vals:
            action = lambda x=val: press(x) if x not in ('=',) else equalpress()
            tk.Button(row, text=val, font=('Arial', 18), command=action).pack(side='left', expand=True, fill='both')

    tk.Button(calc_win, text="Clear", font=('Arial', 18), command=clear).pack(fill='both', padx=10, pady=5)

    return calc_win
