#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
main.py — Recebe datagramas da Pico e:
   • axis 0,1 → move o mouse
   • axis 2,3 → mapeia em W/A/S/D (key-down/ key-up)
"""

import sys
import glob
import serial
import pyautogui
import tkinter as tk
from tkinter import ttk, messagebox

# desativa o “foolproof corner”
pyautogui.FAILSAFE = False

# armazena qual tecla está pressionada por eixo 2 e 3
current_keys = {2: None, 3: None}

def move_mouse(axis, value):
    if axis == 0:
        pyautogui.moveRel(value, 0)
    elif axis == 1:
        pyautogui.moveRel(0, value)

def process_wasd(axis, value):
    """
    axis==2: joystick X → 'd' (value>0) / 'a' (value<0)
    axis==3: joystick Y → 'w' (value>0) / 's' (value<0)
    """
    global current_keys
    if axis == 2:
        key = 'd' if value > 0 else 'a' if value < 0 else None
    elif axis == 3:
        key = 'w' if value > 0 else 's' if value < 0 else None
    else:
        return

    prev = current_keys[axis]
    # se mudou de estado, solta a anterior e aperta a nova
    if key != prev:
        if prev:
            pyautogui.keyUp(prev)
        if key:
            pyautogui.keyDown(key)
        current_keys[axis] = key

def parse_data(data):
    axis  = data[0]
    value = int.from_bytes(data[1:3], byteorder='little', signed=True)
    return axis, value

def controle(ser):
    """
    Loop bloqueante: sincroniza em 0xFF, lê 3 bytes e dispara ação.
    """
    while True:
        sync = ser.read(1)
        if not sync:
            continue
        if sync[0] != 0xFF:
            continue

        chunk = ser.read(3)
        if len(chunk) < 3:
            continue

        axis, value = parse_data(chunk)
        if axis in (0, 1):
            move_mouse(axis, value)
        elif axis in (2, 3):
            process_wasd(axis, value)

def serial_ports():
    ports = []
    if sys.platform.startswith('win'):
        for i in range(1, 256):
            name = f'COM{i}'
            try:
                s = serial.Serial(name); s.close()
                ports.append(name)
            except:
                pass
    elif sys.platform.startswith(('linux','cygwin')):
        ports = glob.glob('/dev/tty[A-Za-z]*')
    elif sys.platform.startswith('darwin'):
        ports = glob.glob('/dev/tty.*')
    return ports

def conectar_porta(port, root, btn, status_lbl, change_circle):
    if not port:
        messagebox.showwarning("Aviso", "Selecione uma porta antes.")
        return
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        status_lbl.config(text=f"Conectado em {port}", fg="green")
        change_circle("green")
        btn.config(text="Conectado")
        root.update()
        controle(ser)
    except KeyboardInterrupt:
        pass
    except Exception as e:
        messagebox.showerror("Erro", f"Não foi possível abrir {port}\n{e}")
        change_circle("red")
    finally:
        try: ser.close()
        except: pass
        status_lbl.config(text="Desconectado", fg="red")
        change_circle("red")

def criar_janela():
    root = tk.Tk()
    root.title("Controle de Mouse+WASD")
    root.geometry("450x280")
    dark_bg = "#2e2e2e"; dark_fg = "#fff"; accent = "#007acc"
    root.configure(bg=dark_bg)

    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure("TLabel", background=dark_bg, foreground=dark_fg)
    style.configure("TButton", background="#444444", foreground=dark_fg)
    style.map("TButton", background=[("active","#555555")])
    style.configure("Accent.TButton", background=accent, foreground=dark_fg)

    # Frame principal
    frm = ttk.Frame(root, padding=20); frm.pack(fill="both", expand=True)
    ttk.Label(frm, text="Mouse + WASD Controller", font=("Segoe UI", 14, "bold")).pack(pady=10)
    porta_var = tk.StringVar()
    btn = ttk.Button(frm, text="Conectar", style="Accent.TButton",
                     command=lambda: conectar_porta(porta_var.get(), root, btn, lbl, change_circle))
    btn.pack(pady=5)

    # Footer: status + dropdown + círculo
    f2 = tk.Frame(root, bg=dark_bg); f2.pack(side="bottom", fill="x", padx=10, pady=10)
    lbl = tk.Label(f2, text="Pronto", bg=dark_bg, fg=dark_fg)
    lbl.grid(row=0, column=0, sticky="w")
    ports = serial_ports()
    if ports: porta_var.set(ports[0])
    cb = ttk.Combobox(f2, textvariable=porta_var, values=ports, state="readonly", width=10)
    cb.grid(row=0, column=1, padx=10)
    circle = tk.Canvas(f2, width=20, height=20, bg=dark_bg, highlightthickness=0)
    ci = circle.create_oval(2,2,18,18, fill="red")
    circle.grid(row=0, column=2, sticky="e")
    f2.columnconfigure(1, weight=1)

    def change_circle(color):
        circle.itemconfig(ci, fill=color)

    root.mainloop()

if __name__ == "__main__":
    criar_janela()
