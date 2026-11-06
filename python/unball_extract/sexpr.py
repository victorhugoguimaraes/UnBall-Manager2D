"""Leitor de S-expressions: preserva strings e não interpreta comandos."""
import json
import re

TOKEN = re.compile(r'\s*(\(|\)|"(?:\\.|[^"\\])*"|[^\s()"]+)')


class Quoted(str):
    pass


def parse_many(text):
    stack, roots, position = [], [], 0
    for match in TOKEN.finditer(text):
        if text[position:match.start()].strip():
            raise ValueError("Texto inválido fora de um registro")
        position = match.end()
        token = match[1]
        if token == '(':
            item = []
            (stack[-1] if stack else roots).append(item)
            stack.append(item)
        elif token == ')':
            if not stack:
                raise ValueError("Parêntese de fechamento sem abertura")
            stack.pop()
        else:
            if not stack:
                raise ValueError("Átomo fora de um registro")
            if token.startswith('"'):
                # RCSS escapes quotes/backslashes, not arbitrary Python expressions.
                value = Quoted(re.sub(r'\\(["\\])', r'\1', token[1:-1]))
            else:
                value = token
            stack[-1].append(value)
    if stack or text[position:].strip():
        raise ValueError("Registro incompleto")
    return roots


def render(value):
    if isinstance(value, list):
        return '(' + ' '.join(map(render, value)) + ')'
    return json.dumps(str(value), ensure_ascii=False) if isinstance(value, Quoted) else str(value)
