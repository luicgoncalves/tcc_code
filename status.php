<?php
header("Content-Type: application/json");

// Configuração do fuso horário (ajuste conforme necessário)
date_default_timezone_set("America/Sao_Paulo");

// Testa a conexão com o banco de dados (se necessário)
$servidor = "localhost";
$usuario = "user";
$senha = "senha";
$banco = "banco"; // Altere para o nome do seu banco de dados

$conn = mysqli_connect($servidor, $usuario, $senha, $banco);

if (!$conn) {
    echo json_encode([
        "status" => "erro",
        "mensagem" => "Banco de dados indisponivel",
        "codigo" => mysqli_connect_errno()
    ]);
    exit();
}

// Se chegou até aqui, a API e o banco estão funcionando
echo json_encode([
    "status" => "OK",
    "mensagem" => "API disponivel",
    "hora" => date("d-m-Y H:i:s")
]);

mysqli_close($conn);
?>
