import tkinter as tk
import numpy as np

class MatrixCalculator:
    def __init__(self, root, on_close_callback=None):
        self.root = tk.Toplevel(root)  # Use Toplevel instead of root for a new window
        self.root.title("Matrix Calculator")
        self.root.geometry("600x400")
        self.on_close_callback = on_close_callback
        print(f"MatrixCalculator initialized: {self.root}")  # Debug initialization

        # Frame for input
        self.input_frame = tk.Frame(self.root)
        self.input_frame.pack(pady=10)

        # Row and Column inputs
        tk.Label(self.input_frame, text="Rows:").grid(row=0, column=0, padx=5)
        self.rows_var = tk.Entry(self.input_frame, width=5)
        self.rows_var.grid(row=0, column=1, padx=5)
        tk.Label(self.input_frame, text="Columns:").grid(row=0, column=2, padx=5)
        self.cols_var = tk.Entry(self.input_frame, width=5)
        self.cols_var.grid(row=0, column=3, padx=5)

        # Create Matrix button
        tk.Button(self.input_frame, text="Create Matrices", command=self.create_matrices).grid(row=0, column=4, padx=5)

        # Frame for matrices
        self.matrix_frame = tk.Frame(self.root)
        self.matrix_frame.pack(pady=10)

        # Result label
        self.result_label = tk.Label(self.root, text="")
        self.result_label.pack(pady=10)

        

        # Bind WM_DELETE_WINDOW to close_window
        self.root.protocol("WM_DELETE_WINDOW", self.close_window)

    def create_matrices(self):
        try:
            rows = int(self.rows_var.get())
            cols = int(self.cols_var.get())
            if rows <= 0 or cols <= 0:
                raise ValueError("Rows and columns must be positive integers")

            # Clear previous entries
            for widget in self.matrix_frame.winfo_children():
                widget.destroy()

            # Matrix A entries
            tk.Label(self.matrix_frame, text="Matrix A:").grid(row=0, column=0, pady=5)
            self.matrix_a_entries = []
            for i in range(rows):
                row_entries = []
                for j in range(cols):
                    entry = tk.Entry(self.matrix_frame, width=5)
                    entry.grid(row=i+1, column=j, padx=2, pady=2)
                    row_entries.append(entry)
                self.matrix_a_entries.append(row_entries)

            # Matrix B entries
            tk.Label(self.matrix_frame, text="Matrix B:").grid(row=rows+1, column=0, pady=5)
            self.matrix_b_entries = []
            for i in range(rows):
                row_entries = []
                for j in range(cols):
                    entry = tk.Entry(self.matrix_frame, width=5)
                    entry.grid(row=i+rows+2, column=j, padx=2, pady=2)
                    row_entries.append(entry)
                self.matrix_b_entries.append(row_entries)

            # Operation buttons
            button_frame = tk.Frame(self.matrix_frame)
            button_frame.grid(row=rows+rows+3, column=0, columnspan=cols, pady=10)
            tk.Button(button_frame, text="Add", command=self.add_matrices).pack(side=tk.LEFT, padx=5)
            tk.Button(button_frame, text="Subtract", command=self.subtract_matrices).pack(side=tk.LEFT, padx=5)
            tk.Button(button_frame, text="Multiply", command=self.multiply_matrices).pack(side=tk.LEFT, padx=5)

        except ValueError as e:
            self.result_label.config(text=f"Error: {str(e)}")

    def get_matrix(self, entries):
        return np.array([[float(entry.get()) for entry in row] for row in entries])

    def add_matrices(self):
        try:
            matrix_a = self.get_matrix(self.matrix_a_entries)
            matrix_b = self.get_matrix(self.matrix_b_entries)
            result = matrix_a + matrix_b
            self.result_label.config(text=f"Result:\n{result}")
        except Exception as e:
            self.result_label.config(text=f"Error: {str(e)}")

    def subtract_matrices(self):
        try:
            matrix_a = self.get_matrix(self.matrix_a_entries)
            matrix_b = self.get_matrix(self.matrix_b_entries)
            result = matrix_a - matrix_b
            self.result_label.config(text=f"Result:\n{result}")
        except Exception as e:
            self.result_label.config(text=f"Error: {str(e)}")

    def multiply_matrices(self):
        try:
            matrix_a = self.get_matrix(self.matrix_a_entries)
            matrix_b = self.get_matrix(self.matrix_b_entries)
            if matrix_a.shape[1] != matrix_b.shape[0]:
                raise ValueError("Number of columns in Matrix A must equal number of rows in Matrix B")
            result = np.dot(matrix_a, matrix_b)
            self.result_label.config(text=f"Result:\n{result}")
        except Exception as e:
            self.result_label.config(text=f"Error: {str(e)}")

    def close_window(self):
        """Close the window and notify the callback with a delay to ensure event processing."""
        print(f"Closing MatrixCalculator: {self.root}")  # Debug close initiation
        if self.on_close_callback:
            self.root.after(100, lambda: self.on_close_callback(self.root))  # Delayed callback
        if self.root:
            self.root.destroy()
            print(f"Destroyed MatrixCalculator: {self.root}")  # Debug destruction

def main(root, on_close_callback=None):
    """Main entry point to create and return the Toplevel window."""
    calc = MatrixCalculator(root, on_close_callback=on_close_callback)
    return calc.root  # Return the Toplevel window, not the class instance

if __name__ == "__main__":
    root = tk.Tk()
    app = MatrixCalculator(root)
    root.mainloop()